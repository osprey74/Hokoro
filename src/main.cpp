// =====================================================================
//  main.cpp  —  Hokoro: Atom Matrix Office Status Indicator
//
//  通常モード (NORMAL):
//    - Cloudflare Worker の予定 JSON を取得
//    - 進行中  : 残量ドット ⇄ MTG スクロールをトグル表示
//    - 5分前  : 増加ドット ⇄ 開始時刻 (HH:MM) をトグル表示 (橙黄)
//    - 超過   : 赤ドット全点灯・点滅
//    - 待機   : 消灯
//  手動ステータスモード (MANUAL):
//    - ボタン短押しで LUNCH → AWAY → BREAK を巡回
//    - 各ステータス文字を横スクロール表示
//
//  ボタン操作 (M5Atom 内蔵ボタン / G39):
//    - 通常/手動モード中の短押し : NORMAL/MANUAL 巡回 (NORMAL では 400ms 遅延発火)
//    - NORMAL ダブルクリック (進行中/超過時): 予定を「終了確定」して dismiss
//    - NORMAL ダブルクリック (idle 時)     : FONT_TEST モードへ (フォント確認)
//    - 通常/手動モード中の長押し2秒: WIFI_SELECT (Wi-Fi プロファイル切替) へ
//    - WIFI_SELECT 中の短押し    : 候補プロファイル巡回 (上段ドットで番号表示)
//    - WIFI_SELECT 中の長押し1秒 : 候補プロファイルへ接続切替を確定
//    - FONT_TEST 中の短押し      : 次のグリフへ即時遷移
//    - FONT_TEST 中の長押し1秒   : NORMAL へ復帰
//    ※ デバウンスは M5.Btn が吸収
//
//  Target: M5Stack Atom Matrix v1.1 (ESP32-PICO-D4)
// =====================================================================
#include <M5Atom.h>
#include <WiFi.h>
#include "secrets.h"      // ← secrets.example.h をコピーして作成
#include "display.h"
#include "schedule.h"
#include "wifi_profiles.h"

// ---------------- 設定値 ----------------
static const uint32_t POLL_MS         = 60UL * 1000UL;  // 予定 JSON 取得間隔(1分)
static const uint32_t WIFI_CHECK_MS   = 30UL * 1000UL;  // Wi-Fi 切断監視間隔(30秒)
static const uint32_t SCROLL_STEP_MS  = 200;            // スクロール速度
static const uint32_t TOGGLE_MS       = 10000;          // 残量/MTG切替間隔
static const uint32_t BLINK_MS        = 500;            // 超過点滅間隔
static const uint32_t LONGPRESS_MS    = 2000;           // NORMAL/MANUAL→WIFI_SELECT 入りの長押し閾値
static const uint32_t LONGPRESS_WS_MS = 1000;           // WIFI_SELECT 中の確定長押し閾値
static const uint32_t DOUBLE_CLICK_MS = 400;            // ダブルクリック判定窓 (NORMAL モードのみ)
static const long     PRE_WARN_SEC    = 5 * 60;         // 開始 N 秒前から予告表示

// ---------------- モード定義 ----------------
enum class Mode { NORMAL, MANUAL, WIFI_SELECT, FONT_TEST };
enum class Manual { LUNCH, AWAY, BREAK };

static Mode    g_mode   = Mode::NORMAL;
static Manual  g_manual = Manual::LUNCH;
static int     g_wifiCandidate = 0;   // WIFI_SELECT 中の選択候補 index

// FONT_TEST モード
static size_t   g_fontTestIdx     = 0;
static uint32_t g_fontTestStartMs = 0;
static const uint32_t FONT_TEST_MS = 2500;     // 1グリフあたりの自動進み時間

// ボタン長押し検出用
static uint32_t g_btnPressStartMs = 0;
static bool     g_btnLongFired    = false;

// ダブルクリック検出用 (NORMAL モードのみ)
static uint32_t g_btnLastReleaseMs = 0;   // 0 = pending なし

// MANUAL モード突入時刻 (経過時間ドット表示用、ステータス遷移ごとにリセット)
static uint32_t g_manualStartMs = 0;

// WIFI_SELECT 進入前のモード (退出時に復元するため記憶)
static Mode    g_modeBeforeWifiSelect   = Mode::NORMAL;
static Manual  g_manualBeforeWifiSelect = Manual::LUNCH;

// 「終了確定」状態
static schedule::Event g_currentActiveEvent;   // renderNormal で毎フレーム更新
static time_t          g_dismissedEnd = 0;     // dismiss された予定の end epoch

// ---------------- 状態 ----------------
static schedule::List g_eventList;
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

// UTC epoch を JST 文字列 "MM/DD HH:MM" に整形（ログ可読化用）
static String fmtJst(time_t epoch) {
  time_t jst = epoch + 9 * 3600;
  struct tm tm;
  gmtime_r(&jst, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%m/%d %H:%M", &tm);
  return String(buf);
}

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
  g_eventList.valid = true;
  g_eventList.count = 1;
  g_eventList.events[0].start = now;
  g_eventList.events[0].end   = now + 60;  // 60秒で終了 → 以降は赤点滅
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
  wifip::loadCurrent();
  Serial.printf("[wifi] %d profile(s) configured, starting from #%d\n",
                wifip::COUNT, wifip::current() + 1);
  wifip::connectAuto();
}

static void syncTime() {
  Serial.println("[ntp] syncing...");
  // NTP。比較は epoch(UTC) で統一するため gmtoffset=0 で取得。
  configTime(0, 0, NTP_SERVER);
  uint32_t t0 = millis();
  while (time(nullptr) < 100000 && millis() - t0 < 8000) delay(200);
  time_t now = time(nullptr);
  if (now > 100000) {
    Serial.printf("[ntp] synced, epoch=%ld\n", (long)now);
  } else {
    Serial.println("[ntp] FAILED");
  }
}

// ---------------- ボタン ----------------
// WIFI_SELECT 退出時の共通処理: 進入前のモードへ復元
static void exitWifiSelectTo(const char* via) {
  g_mode = g_modeBeforeWifiSelect;
  if (g_mode == Mode::MANUAL) {
    g_manual = g_manualBeforeWifiSelect;
    setScrollText(manualLabel());
    Serial.printf("[wifi-select] -> MANUAL/%s (%s)\n", manualLabel(), via);
  } else {
    Serial.printf("[wifi-select] -> NORMAL (%s)\n", via);
  }
}

// 確定処理: 候補プロファイルへの接続切替
static void confirmWifiSelection() {
  Serial.printf("[wifi-select] confirming profile #%d\n", g_wifiCandidate + 1);

  // 接続中フィードバック (シアン全面塗り)
  disp::drawFill(disp::COL_WIFI);
  delay(200);

  bool ok = wifip::tryConnect(g_wifiCandidate);

  // 結果フィードバック (緑/赤を 1秒間表示)
  disp::drawFill(ok ? disp::COL_WIFI_OK : disp::COL_WIFI_NG);
  delay(1000);
  M5.dis.clear();

  if (ok) {
    wifip::saveCurrent(g_wifiCandidate);
    syncTime();           // NTP 再同期
    g_lastPoll = 0;       // 即時スケジュール再取得
    exitWifiSelectTo("confirmed");
  } else {
    // 失敗時は WIFI_SELECT に留まり、別プロファイルを試せるようにする
    Serial.println("[wifi-select] connection failed, staying in WIFI_SELECT");
  }
}

// 短押し: モード内で状態を進める
static void onButtonShortPress() {
  if (g_mode == Mode::NORMAL) {
    g_mode = Mode::MANUAL;
    g_manual = Manual::LUNCH;
    setScrollText(manualLabel());
    g_manualStartMs = millis();
    Serial.println("[btn] -> LUNCH");
  } else if (g_mode == Mode::MANUAL) {
    switch (g_manual) {
      case Manual::LUNCH:
        g_manual = Manual::AWAY;  setScrollText(manualLabel());
        g_manualStartMs = millis();
        Serial.println("[btn] -> AWAY"); break;
      case Manual::AWAY:
        g_manual = Manual::BREAK; setScrollText(manualLabel());
        g_manualStartMs = millis();
        Serial.println("[btn] -> BREAK"); break;
      case Manual::BREAK:
        g_mode = Mode::NORMAL;
        g_lastPoll = 0; /*即時更新*/
        Serial.println("[btn] -> NORMAL");
#ifdef HOKORO_SIM
        initSimEvent();   // ダミー予定をリセットして再スタート
#endif
        break;
    }
  } else if (g_mode == Mode::WIFI_SELECT) {
    // プロファイル候補をローテート
    g_wifiCandidate = (g_wifiCandidate + 1) % wifip::COUNT;
    Serial.printf("[wifi-select] candidate -> #%d\n", g_wifiCandidate + 1);
  } else if (g_mode == Mode::FONT_TEST) {
    // 次のグリフへ進める (即時)
    g_fontTestIdx = (g_fontTestIdx + 1) % disp::FONT_COUNT;
    g_fontTestStartMs = millis();
  }
}

// 長押し: WIFI_SELECT への遷移、WIFI_SELECT 内での確定、または FONT_TEST 退出
static void onButtonLongPress() {
  if (g_mode == Mode::NORMAL || g_mode == Mode::MANUAL) {
    // 退出時の復元のため、進入前のモードを保存
    g_modeBeforeWifiSelect = g_mode;
    g_manualBeforeWifiSelect = g_manual;
    g_mode = Mode::WIFI_SELECT;
    g_wifiCandidate = wifip::current();
    Serial.printf("[btn-long] -> WIFI_SELECT (current=#%d, from=%s)\n",
                  g_wifiCandidate + 1,
                  g_modeBeforeWifiSelect == Mode::MANUAL ? manualLabel() : "NORMAL");
  } else if (g_mode == Mode::WIFI_SELECT) {
    confirmWifiSelection();
  } else if (g_mode == Mode::FONT_TEST) {
    Serial.println("[btn-long] FONT_TEST -> NORMAL");
    g_mode = Mode::NORMAL;
  }
}

// ダブルクリック: 進行中/超過の予定があれば「終了確定」で dismiss、
//                 NORMAL で idle なら FONT_TEST モードへ入る。
static void onButtonDoubleClick() {
  if (g_mode != Mode::NORMAL) return;
  if (g_currentActiveEvent.start != 0) {
    g_dismissedEnd = g_currentActiveEvent.end;
    Serial.printf("[btn-dc] event dismissed (end=%s)\n",
                  fmtJst(g_dismissedEnd).c_str());
    M5.dis.clear();
  } else {
    g_mode = Mode::FONT_TEST;
    g_fontTestIdx = 0;
    g_fontTestStartMs = 0;
    Serial.println("[btn-dc] -> FONT_TEST");
  }
}

// ボタン入力ハンドラ: M5.update() の後に毎ループ呼ぶ
//   NORMAL モード: short-press を DOUBLE_CLICK_MS 遅延発火、その間に2度目の release があれば
//                  double-click として確定する。
//   それ以外    : short-press は即時発火 (cycle 操作のレスポンス重視)。
static void handleButton() {
  uint32_t nowMs = millis();

  if (M5.Btn.wasPressed()) {
    g_btnPressStartMs = nowMs;
    g_btnLongFired    = false;
  }

  // 長押し判定
  if (M5.Btn.isPressed() && !g_btnLongFired) {
    uint32_t held = nowMs - g_btnPressStartMs;
    uint32_t thresh = (g_mode == Mode::WIFI_SELECT || g_mode == Mode::FONT_TEST)
                      ? LONGPRESS_WS_MS : LONGPRESS_MS;
    if (held >= thresh) {
      onButtonLongPress();
      g_btnLongFired = true;
      g_btnLastReleaseMs = 0;          // pending な short-press をキャンセル
    }
  }

  // リリース処理
  if (M5.Btn.wasReleased() && !g_btnLongFired) {
    if (g_mode == Mode::NORMAL) {
      if (g_btnLastReleaseMs != 0 && nowMs - g_btnLastReleaseMs < DOUBLE_CLICK_MS) {
        // 2度目の release: double-click 確定
        onButtonDoubleClick();
        g_btnLastReleaseMs = 0;
      } else {
        // 1度目の release: double-click 窓を開始 (single short-press は遅延発火)
        g_btnLastReleaseMs = nowMs;
      }
    } else {
      // MANUAL / WIFI_SELECT は遅延なしで cycle
      onButtonShortPress();
    }
  }

  // double-click 窓を超過しても2度目が来なければ single short-press 発火
  if (g_btnLastReleaseMs != 0 && nowMs - g_btnLastReleaseMs >= DOUBLE_CLICK_MS) {
    onButtonShortPress();
    g_btnLastReleaseMs = 0;
  }
}

// ---------------- 通常モード描画 ----------------
//   状態優先順位:
//     1. 進行中の予定 (start <= now <= end)             → 残量ドット ⇄ MTG
//     2. 5分前予告 (0 < start - now <= PRE_WARN_SEC)    → 増加ドット ⇄ 開始時刻
//     3. 超過点滅 (end < now <= end + grace)            → 赤点滅
//     4. idle                                          → 消灯
static void renderNormal(uint32_t nowMs) {
#ifndef HOKORO_SIM
  // ポーリング（実機ビルドのみ。シミュレータでは Wi-Fi/取得をスキップ）
  if (WiFi.status() == WL_CONNECTED &&
      (g_lastPoll == 0 || nowMs - g_lastPoll >= POLL_MS)) {
    g_lastPoll = nowMs;
    Serial.println("[sched] fetching...");
    schedule::List fetched = schedule::fetchAll(SCHEDULE_URL);
    if (fetched.valid) {
      g_eventList = fetched;   // 成功時のみ置換 (失敗時は前回データを保持)
    }
  }
#endif

  time_t now = nowJst();

  // 進行中 or 直近終了の予定を選出
  bool isOver = false;
  schedule::Event ev = schedule::currentOrJustEnded(g_eventList, now, isOver, g_dismissedEnd);

  // 5分前予告候補 (進行中時は不要なのでスキップして高速化)
  bool isInProgress = (ev.start != 0 && !isOver);
  schedule::Event upcoming;
  if (!isInProgress) {
    upcoming = schedule::upcomingWithin(g_eventList, now, PRE_WARN_SEC, g_dismissedEnd);
  }

  // 状態遷移ログ (4状態いずれかが変わったとき)
  static time_t s_lastStart = -1;
  static bool   s_lastOver  = false;
  static time_t s_lastUpcomingStart = -1;
  if (ev.start != s_lastStart || isOver != s_lastOver
      || upcoming.start != s_lastUpcomingStart) {
    if (isInProgress) {
      Serial.printf("[sched] active: start=%s end=%s now=%s\n",
                    fmtJst(ev.start).c_str(), fmtJst(ev.end).c_str(),
                    fmtJst(now).c_str());
    } else if (upcoming.start != 0) {
      long secToStart = upcoming.start - now;
      Serial.printf("[sched] prewarn: start=%s (%lds away) now=%s\n",
                    fmtJst(upcoming.start).c_str(), (long)secToStart,
                    fmtJst(now).c_str());
    } else if (ev.start != 0 && isOver) {
      Serial.printf("[sched] over: end=%s now=%s\n",
                    fmtJst(ev.end).c_str(), fmtJst(now).c_str());
    } else {
      Serial.printf("[sched] idle (now=%s)\n", fmtJst(now).c_str());
    }
    s_lastStart = ev.start;
    s_lastOver  = isOver;
    s_lastUpcomingStart = upcoming.start;
    // 状態が変わったらトグルとスクロールをリセット
    g_lastToggle = nowMs;
    g_showProg = true;
    g_scrollOff = -5;
  }

  // ---------------- 状態別描画 ----------------
  if (isInProgress) {
    g_currentActiveEvent = ev;          // ダブルクリックで dismiss 可
    long total = ev.end - ev.start;
    long left  = ev.end - now;
    if (nowMs - g_lastToggle >= TOGGLE_MS) {
      g_lastToggle = nowMs;
      g_showProg = !g_showProg;
      if (!g_showProg) setScrollText("MTG");
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
    return;
  }

  if (upcoming.start != 0) {
    g_currentActiveEvent = upcoming;    // 予告中も dismiss 可 (この予定を消したい場合)
    long secToStart = upcoming.start - now;
    if (secToStart < 0) secToStart = 0;
    if (secToStart > PRE_WARN_SEC) secToStart = PRE_WARN_SEC;
    int lit = (int)(((PRE_WARN_SEC - secToStart) * 25) / PRE_WARN_SEC);
    if (lit > 25) lit = 25;
    if (lit < 0)  lit = 0;

    if (nowMs - g_lastToggle >= TOGGLE_MS) {
      g_lastToggle = nowMs;
      g_showProg = !g_showProg;
      if (!g_showProg) {
        // 開始時刻 "HH:MM" (JST) をスクロールテキストに
        time_t jst = upcoming.start + 9 * 3600;
        struct tm tm;
        gmtime_r(&jst, &tm);
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        setScrollText(String(buf));
      }
    }
    if (g_showProg) {
      disp::drawProgress(lit, disp::COL_PREWARN, false, false);
    } else {
      if (nowMs - g_lastScroll >= SCROLL_STEP_MS) {
        g_lastScroll = nowMs;
        disp::drawScrollFrame(g_cols, g_nCols, g_scrollOff, disp::COL_PREWARN);
        if (++g_scrollOff > (int)g_nCols) g_scrollOff = -5;
      }
    }
    return;
  }

  if (ev.start != 0 && isOver) {
    g_currentActiveEvent = ev;          // ダブルクリックで dismiss 可
    if (nowMs - g_lastBlink >= BLINK_MS) {
      g_lastBlink = nowMs; g_blinkOn = !g_blinkOn;
    }
    disp::drawProgress(0, disp::COL_OVER, true, g_blinkOn);
    return;
  }

  // idle
  g_currentActiveEvent = schedule::Event();
  M5.dis.clear();
}

// ---------------- 手動モード描画 ----------------
//   文字スクロール ⇄ 経過時間ドット (1ドット=1分) を TOGGLE_MS でトグル。
//   ステータス遷移時に g_manualStartMs を更新し、各ステータスごとの経過を表示。
static void renderManual(uint32_t nowMs) {
  if (nowMs - g_lastToggle >= TOGGLE_MS) {
    g_lastToggle = nowMs;
    g_showProg = !g_showProg;
  }

  if (g_showProg) {
    // 経過分数を 0..25 ドットで点灯 (25分以降は満タン)
    uint32_t elapsedMin = (nowMs - g_manualStartMs) / 60000UL;
    int lit = (elapsedMin > 25) ? 25 : (int)elapsedMin;
    disp::drawProgress(lit, manualColor(), false, false);
  } else {
    if (nowMs - g_lastScroll >= SCROLL_STEP_MS) {
      g_lastScroll = nowMs;
      disp::drawScrollFrame(g_cols, g_nCols, g_scrollOff, manualColor());
      if (++g_scrollOff > (int)g_nCols) g_scrollOff = -5;
    }
  }
}

// ---------------- Wi-Fi 選択モード描画 ----------------
//   選択中プロファイル番号を上段のN個ドットで表示。
//   フォント不要、回路チェック前でも安全に表示可能。
static void renderWifiSelect(uint32_t /*nowMs*/) {
  disp::drawProfileIndicator(g_wifiCandidate + 1, disp::COL_WIFI);
}

// ---------------- FONT_TEST モード描画 ----------------
//   実装済みグリフを中央寄せで1文字ずつ表示し、FONT_TEST_MS 毎に自動で次へ。
//   ボタン短押しで即座に次へ、長押し1秒で NORMAL 復帰。
static void renderFontTest(uint32_t nowMs) {
  if (g_fontTestStartMs == 0) {
    g_fontTestStartMs = nowMs;
  } else if (nowMs - g_fontTestStartMs >= FONT_TEST_MS) {
    g_fontTestStartMs = nowMs;
    g_fontTestIdx = (g_fontTestIdx + 1) % disp::FONT_COUNT;
  }
  disp::drawGlyphCentered(&disp::FONT[g_fontTestIdx], disp::COL_AWAY);

  // シリアルへ現在表示中のグリフを (idx 変更時のみ) 出力
  static size_t s_lastLoggedIdx = (size_t)-1;
  if (g_fontTestIdx != s_lastLoggedIdx) {
    const disp::Glyph& g = disp::FONT[g_fontTestIdx];
    Serial.printf("[font-test] [%u/%u] '%c' (width=%u)\n",
                  (unsigned)(g_fontTestIdx + 1),
                  (unsigned)disp::FONT_COUNT,
                  g.c, (unsigned)g.width);
    s_lastLoggedIdx = g_fontTestIdx;
  }
}

// ---------------- Arduino ----------------
void setup() {
  M5.begin(true, false, true); // Serial, I2C=false, Display=true
  M5.dis.clear();
  delay(200);                  // シリアル安定化
  Serial.println();
  Serial.println("=== Hokoro starting ===");
#ifdef HOKORO_SIM
  Serial.println("[mode] HOKORO_SIM (sim event injected, wifi/ical skipped)");
  initSimEvent();              // Wi-Fi/NTPをスキップしダミー予定で起動
#else
  connectWifi();
  syncTime();
#endif
  setScrollText("MTG");
  g_lastToggle = millis();
  Serial.println("[setup] done, entering loop");
}

// Wi-Fi 切断監視: WIFI_CHECK_MS ごとに status を見て、切れていれば再接続を試みる。
static void watchWifi(uint32_t nowMs) {
#ifdef HOKORO_SIM
  (void)nowMs;
  return;
#else
  static uint32_t s_lastCheck = 0;
  if (nowMs - s_lastCheck < WIFI_CHECK_MS) return;
  s_lastCheck = nowMs;
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("[wifi] connection lost, attempting reconnect...");
  if (wifip::connectAuto()) {
    syncTime();         // 時刻ドリフト是正のため NTP 再同期
    g_lastPoll = 0;     // 復帰直後にスケジュール再取得
    // WIFI_SELECT で confirm 失敗後に取り残されている場合は進入前モードへ復帰
    if (g_mode == Mode::WIFI_SELECT) {
      exitWifiSelectTo("auto-recovered");
    }
  }
#endif
}

void loop() {
  M5.update();
  handleButton();

  uint32_t nowMs = millis();
  watchWifi(nowMs);

  switch (g_mode) {
    case Mode::NORMAL:      renderNormal(nowMs);      break;
    case Mode::MANUAL:      renderManual(nowMs);      break;
    case Mode::WIFI_SELECT: renderWifiSelect(nowMs);  break;
    case Mode::FONT_TEST:   renderFontTest(nowMs);    break;
  }

  delay(10);
}
