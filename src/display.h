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

// WIFI_SELECT モード用
constexpr uint32_t COL_WIFI     = 0x0080C0; // 選択中インジケータ: シアン
constexpr uint32_t COL_WIFI_OK  = 0x00C040; // 接続成功フィードバック: 緑
constexpr uint32_t COL_WIFI_NG  = 0xC02000; // 接続失敗フィードバック: 赤

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

// drawpix ラッパ。設置向きに合わせて idx を変換する。
//   シミュレータ: Wokwi のチェイン方向差を補正する 180° 回転 (idx → 24 - idx)
//   実機       : 物理設置向きに合わせて縦反転 (= 180° + 左右反転)
inline void put(int idx, uint32_t color) {
#ifdef HOKORO_SIM
  idx = 24 - idx;
#else
  int x = idx % 5;
  int y = idx / 5;
  idx = (4 - y) * 5 + x;   // 縦反転
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
//   実機: 物理 top-left (drawpix(0)) から右へ消えていく順。
//   sim : Wokwi のチェイン方向に従い put() 経由で従来挙動を維持。
inline void drawProgress(int litDots, uint32_t color,
                         bool over, bool blinkOn) {
  M5.dis.clear();
  if (over) {
    if (blinkOn) for (int i = 0; i < 25; ++i) put(i, COL_OVER);
    return;
  }
  litDots = constrain(litDots, 0, 25);
#ifdef HOKORO_SIM
  for (int i = 0; i < litDots; ++i) put(i, color);
#else
  // drawpix(0) = 物理 top-left。最初の (25 - litDots) 個を未点灯にする。
  for (int i = 25 - litDots; i < 25; ++i) M5.dis.drawpix(i, color);
#endif
}

// 全面塗りつぶし (接続中フィードバック等で使用)
inline void drawFill(uint32_t color) {
  for (int i = 0; i < 25; ++i) put(i, color);
}

// WIFI_SELECT モード用: 上段(row 0) に N 個ドットを点灯してプロファイル番号を表現。
//   profile 1 → 1 ドット、profile 3 → 3 ドット
inline void drawProfileIndicator(int oneBasedIndex, uint32_t color) {
  M5.dis.clear();
  int n = constrain(oneBasedIndex, 0, 5);
  for (int x = 0; x < n; ++x) {
    put(0 * 5 + x, color);  // row 0
  }
}

} // namespace disp
