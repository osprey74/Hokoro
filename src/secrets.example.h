#pragma once
// =====================================================================
//  secrets.h  —  このファイルは .gitignore 対象。実値はここに記入。
//  リポジトリには secrets.example.h のみコミットすること。
// =====================================================================

// ---- Wi-Fi 接続情報（2.4GHz のみ。ESP32-PICO-D4 は 5GHz 非対応） ----
// プライマリプロファイル (必須)
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// 追加プロファイル (任意。不要なら以下のブロックを削除またはコメントアウト)
// 自動フォールバック対象。長押し2秒の WIFI_SELECT モードで巡回選択可能。
//#define WIFI_SSID_2 "HOME_WIFI_SSID"
//#define WIFI_PASS_2 "HOME_WIFI_PASSWORD"

//#define WIFI_SSID_3 "MOBILE_TETHER_SSID"
//#define WIFI_PASS_3 "MOBILE_TETHER_PASSWORD"

// ---- スケジュール JSON URL (Cloudflare Worker のエンドポイント) ----
//  形式: { "events": [ { "start": <epoch>, "end": <epoch> }, ... ] }
#define SCHEDULE_URL    "https://hokoro.<your-subdomain>.workers.dev/schedule.json"

// ---- タイムゾーン（JST = UTC+9）。NTP 同期に使用。 ----
#define TZ_OFFSET_SEC   (9 * 3600)
#define NTP_SERVER      "ntp.nict.jp"
