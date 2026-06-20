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
constexpr uint32_t COL_PROG    = 0x104060; // 残量バー: 青系
constexpr uint32_t COL_OVER    = 0xC00000; // 超過: 赤（点滅）
constexpr uint32_t COL_PREWARN = 0xC07000; // 5分前予告: 橙黄系（増加ドット & 時刻スクロール）
constexpr uint32_t COL_CLOCK   = 0x202040; // idle 時の時計: 控えめな紺紫

// WIFI_SELECT モード用
constexpr uint32_t COL_WIFI     = 0x0080C0; // 選択中インジケータ: シアン
constexpr uint32_t COL_WIFI_OK  = 0x00C040; // 接続成功フィードバック: 緑
constexpr uint32_t COL_WIFI_NG  = 0xC02000; // 接続失敗フィードバック: 赤

// ---- 可変幅ピクセルフォント (5x{1..5} ドット) ----
//  各列5bit、bit r = 表示行 r (0=最上段, 4=最下段)。
//  width = 1〜5。3 を標準、4〜5 を幅広 (A, C, M, N, U, W 等)、1 を細記号に使用。
//  put() による表示向き補正 (実機: 縦反転, sim: 180°) は透過的に効く。
struct Glyph { char c; uint8_t width; uint8_t col[5]; };

static const Glyph FONT[] = {
  // ---- 記号 ----
  {' ', 1, {0x00, 0x00, 0x00, 0x00, 0x00}},
  {':', 1, {0x0A, 0x00, 0x00, 0x00, 0x00}},
  {'?', 5, {0x02, 0x01, 0x15, 0x05, 0x02}},

  // ---- 英字 (3x5) ----
  {'B', 3, {0x1F, 0x15, 0x0A, 0x00, 0x00}},
  {'E', 3, {0x1F, 0x15, 0x11, 0x00, 0x00}},
  {'K', 3, {0x1F, 0x04, 0x1B, 0x00, 0x00}},
  {'L', 3, {0x1F, 0x10, 0x10, 0x00, 0x00}},
  {'R', 3, {0x1F, 0x05, 0x1A, 0x00, 0x00}},
  {'T', 3, {0x01, 0x1F, 0x01, 0x00, 0x00}},
  {'Y', 3, {0x03, 0x1C, 0x03, 0x00, 0x00}},

  // ---- 英字 (4x5) ----
  {'A', 4, {0x1E, 0x05, 0x05, 0x1E, 0x00}},
  {'C', 4, {0x0E, 0x11, 0x11, 0x0A, 0x00}},
  {'G', 4, {0x0E, 0x11, 0x15, 0x1D, 0x00}},
  {'H', 4, {0x1F, 0x04, 0x04, 0x1F, 0x00}},
  {'U', 4, {0x0F, 0x10, 0x10, 0x0F, 0x00}},

  // ---- 英字 (5x5) ----
  {'M', 5, {0x1F, 0x02, 0x04, 0x02, 0x1F}},
  {'N', 5, {0x1F, 0x02, 0x04, 0x08, 0x1F}},
  {'W', 5, {0x1F, 0x08, 0x04, 0x08, 0x1F}},

  // ---- 数字 (3x5) ----
  {'1', 3, {0x12, 0x1F, 0x10, 0x00, 0x00}},
  {'2', 3, {0x1D, 0x15, 0x17, 0x00, 0x00}},
  {'3', 3, {0x15, 0x15, 0x1F, 0x00, 0x00}},
  {'4', 3, {0x07, 0x04, 0x1F, 0x00, 0x00}},
  {'5', 3, {0x17, 0x15, 0x1D, 0x00, 0x00}},
  {'6', 3, {0x1F, 0x15, 0x1D, 0x00, 0x00}},
  {'7', 3, {0x01, 0x19, 0x07, 0x00, 0x00}},
  {'9', 3, {0x17, 0x15, 0x1F, 0x00, 0x00}},

  // ---- 数字 (4x5) ----
  {'0', 4, {0x0E, 0x11, 0x11, 0x0E, 0x00}},
  {'8', 4, {0x0A, 0x15, 0x15, 0x0A, 0x00}},
};

constexpr size_t FONT_COUNT = sizeof(FONT) / sizeof(FONT[0]);

inline const Glyph* glyphFor(char c) {
  for (auto &g : FONT) if (g.c == c) return &g;
  for (auto &g : FONT) if (g.c == '?') return &g;     // fallback
  return nullptr;
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

// 文字列 -> 列ビットマップへ展開 (各文字は width 列 + 1 列スペース)
inline void buildColumns(const String& text, uint8_t* out, size_t& n) {
  n = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const Glyph* g = glyphFor(text[i]);
    if (!g) continue;
    for (uint8_t j = 0; j < g->width; ++j) out[n++] = g->col[j];
    out[n++] = 0x00;  // 文字間スペース
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

// WIFI_SELECT モード用: 物理 top row に N 個ドットを点灯してプロファイル番号を表現。
//   profile 1 → 1 ドット、profile 3 → 3 ドット
//   put() の縦反転を経て物理上段に出るよう logical row 4 (=bit4) を指定。
inline void drawProfileIndicator(int oneBasedIndex, uint32_t color) {
  M5.dis.clear();
  int n = constrain(oneBasedIndex, 0, 5);
  for (int x = 0; x < n; ++x) {
    put(4 * 5 + x, color);  // logical row 4 → 縦反転後の physical row 0
  }
}

// 単一グリフを中央寄せで描画 (FONT_TEST 用)
//   width=3 → x=1..3 / width=4 → x=0..3 / width=5 → x=0..4
inline void drawGlyphCentered(const Glyph* g, uint32_t color) {
  M5.dis.clear();
  if (!g) return;
  int startX = (5 - g->width) / 2;
  if (startX < 0) startX = 0;
  for (int x = 0; x < g->width; ++x) {
    int dx = startX + x;
    if (dx >= 5) break;
    uint8_t colBits = g->col[x];
    for (int y = 0; y < 5; ++y) {
      if (colBits & (0x10 >> y)) {
        put(y * 5 + dx, color);
      }
    }
  }
}

} // namespace disp
