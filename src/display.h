#pragma once
#include <Arduino.h>
#include <M5Atom.h>

// =====================================================================
//  display.h  —  5x5 (25 dot) WS2812C マトリクス描画ヘルパ
//  - 3x5 簡易フォントによる横スクロール
//  - 残り時間プログレス（点灯ドット数 0..25）
//  Atom Matrix の LED index は左上(0)→右へ進み行ごとに折り返す前提
//  （M5.dis.drawpix(index, color) / index = y*5 + x）
// =====================================================================

namespace disp {

// ---- 色定義（CUD 配慮: 色だけに依存せず文字/挙動と併用すること）----
//  CRGB ではなく M5Atom の 0xRRGGBB 形式（drawpix が受け取る形）
constexpr uint32_t COL_OFF    = 0x000000;
constexpr uint32_t COL_MTG    = 0xC03000; // MTG: 橙赤
constexpr uint32_t COL_LUNCH  = 0xA04000; // LUNCH: 橙
constexpr uint32_t COL_AWAY   = 0x6010A0; // AWAY: 紫
constexpr uint32_t COL_BREAK  = 0x108040; // BREAK: 緑
constexpr uint32_t COL_PROG   = 0x104060; // 残量バー: 青系
constexpr uint32_t COL_OVER   = 0xC00000; // 超過: 赤（点滅）

// ---- 3x5 フォント（必要英字のみ。各列5bit、上位bit=最上段）----
//  1 グリフ = 3 列。bit4(0x10)=row0 ... bit0(0x01)=row4
struct Glyph { char c; uint8_t col[3]; };

static const Glyph FONT[] = {
  {'A', {0x1F, 0x05, 0x1F}},
  {'B', {0x1F, 0x15, 0x0A}},
  {'C', {0x1F, 0x11, 0x11}},
  {'E', {0x1F, 0x15, 0x11}},
  {'G', {0x0E, 0x11, 0x1D}},
  {'H', {0x1F, 0x04, 0x1F}},
  {'K', {0x1F, 0x04, 0x1B}},
  {'L', {0x1F, 0x10, 0x10}},
  {'M', {0x1F, 0x02, 0x1F}},
  {'N', {0x1F, 0x0E, 0x1F}},
  {'R', {0x1F, 0x05, 0x1A}},
  {'T', {0x01, 0x1F, 0x01}},
  {'U', {0x1F, 0x10, 0x1F}},
  {'W', {0x1F, 0x08, 0x1F}},
  {'Y', {0x03, 0x1C, 0x03}},
  {' ', {0x00, 0x00, 0x00}},
};

inline const uint8_t* glyphFor(char c) {
  for (auto &g : FONT) if (g.c == c) return g.col;
  return FONT[sizeof(FONT)/sizeof(FONT[0]) - 1].col; // space fallback
}

// drawpix ラッパ。シミュレータ時のみ Wokwi のチェイン方向差を吸収するため
// 180°反転（idx → 24-idx）して描画する。実機ビルドでは素通し。
inline void put(int idx, uint32_t color) {
#ifdef HOKORO_SIM
  idx = 24 - idx;
#endif
  M5.dis.drawpix(idx, color);
}

// 文字列 -> 列ビットマップ（各文字 3列 + 1列スペース）に展開
inline void buildColumns(const String& text, uint8_t* out, size_t& n) {
  n = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const uint8_t* g = glyphFor(text[i]);
    out[n++] = g[0]; out[n++] = g[1]; out[n++] = g[2];
    out[n++] = 0x00; // 文字間スペース
  }
}

// スクロール 1 フレーム描画（offset 列目を左端に表示）
inline void drawScrollFrame(const uint8_t* cols, size_t n,
                            int offset, uint32_t color) {
  M5.dis.clear();
  for (int x = 0; x < 5; ++x) {
    int ci = offset + x;
    uint8_t colBits = (ci >= 0 && ci < (int)n) ? cols[ci] : 0x00;
    for (int y = 0; y < 5; ++y) {
      if (colBits & (0x10 >> y)) {
        put(y * 5 + x, color);
      }
    }
  }
}

// 残り時間プログレス: 0..25 ドットを点灯。over=true で警告色点滅。
inline void drawProgress(int litDots, uint32_t color,
                         bool over, bool blinkOn) {
  M5.dis.clear();
  if (over) {
    if (blinkOn) for (int i = 0; i < 25; ++i) put(i, COL_OVER);
    return;
  }
  litDots = constrain(litDots, 0, 25);
  for (int i = 0; i < litDots; ++i) put(i, color);
}

} // namespace disp
