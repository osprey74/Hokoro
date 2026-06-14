// =====================================================================
//  main.cpp  —  Hokoro: Atom Matrix Office Status Indicator
//
//  通常モード (NORMAL):
//    - iCal の直近予定を取得
//    - 「残り時間ドット表示」と「MTG スクロール」をトグル表示
//      (予定が進行中のときのみ。予定が無ければ全消灯=待機)
//  手動ステータスモード (MANUAL):
//    - ボタン短押しで LUNCH → AWAY → BREAK を巡回
//    - 各ステータス文字を横スクロール表示
//
//  ボタン操作 (M5Atom 内蔵ボタン / G39):
//    - 通常モード中の短押し  : MANUAL へ入り LUNCH 表示
//    - 手動モード中の短押し  : LUNCH→AWAY→BREAK→(NORMAL復帰) を巡回
//    ※ デバウンスは M5.Btn が吸収
//
//  Target: M5Stack Atom Matrix v1.1 (ESP32-PICO-D4)
// =====================================================================
#include <M5Atom.h>
#include <WiFi.h>
#include "secrets.h"      // ← secrets.example.h をコピーして作成
#include "display.h"
#include "ical.h"

// ---------------- 設定値 ----------------
static const uint32_t ICAL_POLL_MS   = 60UL * 1000UL;  // iCal 取得間隔(1分)
static const uint32_t SCROLL_STEP_MS = 200;            // スクロール速度
static const uint32_t TOGGLE_MS      = 10000;          // 残量/MTG切替間隔
static const uint32_t BLINK_MS       = 500;            // 超過点滅間隔

// ---------------- モード定義 ----------------
enum class Mode { NORMAL, MANUAL };
enum class Manual { LUNCH, AWAY, BREAK };

static Mode    g_mode   = Mode::NORMAL;
static Manual  g_manual = Manual::LUNCH;

// ---------------- 状態 ----------------
static ical::Event g_event;
static uint32_t g_lastPoll   = 0;
static uint32_t g_lastScroll = 0;
static uint32_t g_lastToggle = 0;
static uint32_t g_lastBlink  = 0;
static int      g_scrollOff  = -5;   // 画面外左から流入
static bool     g_showProg   = true; // NORMAL: 残量表示/MTGスクロールのトグル
static bool     g_blinkOn    = false;

// スクロール用バッファ
static uint8_t  g_cols[64];
static size_t   g_nCols = 0;
static String   g_scrollText = "";

// ---------------- ユーティリティ ----------------
#ifdef HOKORO_SIM
// 擬似時刻のベース（非ゼロ）。start==0 を「予定なし」と判定する保護ロジックに
// 引っかからないように、適当な epoch 値をベースに加算する。
static constexpr time_t SIM_BASE_EPOCH = 1700000000;
#endif

static time_t nowJst() {
#ifdef HOKORO_SIM
  // シミュレータ時は millis()ベースの擬似時刻（起動からの経過秒 + ベース）
  return SIM_BASE_EPOCH + (time_t)(millis() / 1000);
#else
  time_t t = time(nullptr);
  return t;   // configTime 済みなら epoch(UTC)。比較は epoch 同士で行う。
#endif
}

#ifdef HOKORO_SIM
// シミュレータ用: 60秒間のダミーMTGを注入。経過後は自動的に超過点滅へ遷移。
static void initSimEvent() {
  time_t now = nowJst();
  g_event.valid   = true;
  g_event.start   = now;
  g_event.end     = now + 60;  // 60秒で終了 → 以降は赤点滅
  g_event.summary = "MTG";
}
#endif

static void setScrollText(const String& s) {
  if (s == g_scrollText) return;
  g_scrollText = s;
  disp::buildColumns(s, g_cols, g_nCols);
  g_scrollOff = -5;
}

static uint32_t manualColor() {
  switch (g_manual) {
    case Manual::LUNCH: return disp::COL_LUNCH;
    case Manual::AWAY:  return disp::COL_AWAY;
    case Manual::BREAK: return disp::COL_BREAK;
  }
  return disp::COL_OFF;
}

static const char* manualLabel() {
  switch (g_manual) {
    case Manual::LUNCH: return "LUNCH";
    case Manual::AWAY:  return "AWAY";
    case Manual::BREAK: return "BREAK";
  }
  return "";
}

// ---------------- Wi-Fi / 時刻 ----------------
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
  }
}

static void syncTime() {
  // NTP。比較は epoch(UTC) で統一するため gmtoffset=0 で取得。
  configTime(0, 0, NTP_SERVER);
  uint32_t t0 = millis();
  while (time(nullptr) < 100000 && millis() - t0 < 8000) delay(200);
}

// ---------------- ボタン ----------------
static void onButton() {
  if (g_mode == Mode::NORMAL) {
    g_mode = Mode::MANUAL;
    g_manual = Manual::LUNCH;
    setScrollText(manualLabel());
  } else { // MANUAL: 巡回 → BREAK の次で NORMAL へ復帰
    switch (g_manual) {
      case Manual::LUNCH: g_manual = Manual::AWAY;  setScrollText(manualLabel()); break;
      case Manual::AWAY:  g_manual = Manual::BREAK; setScrollText(manualLabel()); break;
      case Manual::BREAK:
        g_mode = Mode::NORMAL;
        g_lastPoll = 0; /*即時更新*/
#ifdef HOKORO_SIM
        initSimEvent();   // ダミー予定をリセットして再スタート
#endif
        break;
    }
  }
}

// ---------------- 通常モード描画 ----------------
static void renderNormal(uint32_t nowMs) {
#ifndef HOKORO_SIM
  // ポーリング（実機ビルドのみ。シミュレータでは Wi-Fi/iCal をスキップ）
  if (WiFi.status() == WL_CONNECTED &&
      (g_lastPoll == 0 || nowMs - g_lastPoll >= ICAL_POLL_MS)) {
    g_lastPoll = nowMs;
    g_event = ical::fetchNext(ICAL_URL, nowJst());
  }
#endif

  time_t now = nowJst();

  // 進行中の予定が無ければ待機（消灯）
  bool active = g_event.valid && now >= g_event.start && g_event.start != 0;
  if (!active) { M5.dis.clear(); return; }

  // 残量/超過の計算
  long total = g_event.end - g_event.start;
  long left  = g_event.end - now;
  bool over  = left < 0;

  // 表示トグル（残量ドット ⇄ MTG スクロール）
  if (nowMs - g_lastToggle >= TOGGLE_MS) {
    g_lastToggle = nowMs;
    g_showProg = !g_showProg;
    if (!g_showProg) setScrollText("MTG");
  }

  if (over) { // 超過は最優先で点滅
    if (nowMs - g_lastBlink >= BLINK_MS) { g_lastBlink = nowMs; g_blinkOn = !g_blinkOn; }
    disp::drawProgress(0, disp::COL_OVER, true, g_blinkOn);
    return;
  }

  if (g_showProg) {
    int lit = (total > 0) ? (int)((left * 25) / total) : 0;
    disp::drawProgress(lit, disp::COL_PROG, false, false);
  } else {
    if (nowMs - g_lastScroll >= SCROLL_STEP_MS) {
      g_lastScroll = nowMs;
      disp::drawScrollFrame(g_cols, g_nCols, g_scrollOff, disp::COL_MTG);
      if (++g_scrollOff > (int)g_nCols) g_scrollOff = -5;
    }
  }
}

// ---------------- 手動モード描画 ----------------
static void renderManual(uint32_t nowMs) {
  if (nowMs - g_lastScroll >= SCROLL_STEP_MS) {
    g_lastScroll = nowMs;
    disp::drawScrollFrame(g_cols, g_nCols, g_scrollOff, manualColor());
    if (++g_scrollOff > (int)g_nCols) g_scrollOff = -5;
  }
}

// ---------------- Arduino ----------------
void setup() {
  M5.begin(true, false, true); // Serial, I2C=false, Display=true
  M5.dis.clear();
#ifdef HOKORO_SIM
  initSimEvent();              // Wi-Fi/NTPをスキップしダミー予定で起動
#else
  connectWifi();
  syncTime();
#endif
  setScrollText("MTG");
  g_lastToggle = millis();
}

void loop() {
  M5.update();
  if (M5.Btn.wasPressed()) onButton();

  uint32_t nowMs = millis();
  if (g_mode == Mode::NORMAL) renderNormal(nowMs);
  else                        renderManual(nowMs);

  delay(10);
}
