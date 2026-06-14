#pragma once
// =====================================================================
//  secrets.h  —  このファイルは .gitignore 対象。実値はここに記入。
//  リポジトリには secrets.example.h のみコミットすること。
// =====================================================================

// Wi-Fi 接続情報（2.4GHz のみ。ESP32-PICO-D4 は 5GHz 非対応）
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// iCal フィード URL（Google カレンダーの「ical 形式の限定公開 URL」等）
//  - https 推奨。証明書検証は本雛形では簡略化（後述の注意点参照）
#define ICAL_URL    "https://example.com/basic.ics"

// タイムゾーン（JST = UTC+9）。NTP 同期に使用。
#define TZ_OFFSET_SEC   (9 * 3600)
#define NTP_SERVER      "ntp.nict.jp"
