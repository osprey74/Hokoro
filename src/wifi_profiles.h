#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "secrets.h"

// =====================================================================
//  wifi_profiles.h  —  複数 Wi-Fi プロファイル + 自動フォールバック
//
//  - secrets.h で WIFI_SSID(必須), WIFI_SSID_2(任意), WIFI_SSID_3(任意)
//    を定義することで最大3プロファイル登録できる。
//  - 最後に接続成功したプロファイル index を NVS(Preferences) に保存し、
//    次回起動時はそこから順に試行する。
//  - WIFI_SELECT モードでユーザーが明示的にプロファイルを切替可能。
// =====================================================================

namespace wifip {

struct Profile {
  const char* ssid;
  const char* pass;
};

// secrets.h の #define 群からプロファイル配列を構築。
//   WIFI_SSID_2 / WIFI_SSID_3 が #define されていなければ自動的に除外される。
static const Profile g_profiles[] = {
  { WIFI_SSID,   WIFI_PASS   },
#ifdef WIFI_SSID_2
  { WIFI_SSID_2, WIFI_PASS_2 },
#endif
#ifdef WIFI_SSID_3
  { WIFI_SSID_3, WIFI_PASS_3 },
#endif
};

constexpr int COUNT = sizeof(g_profiles) / sizeof(g_profiles[0]);

// 現在選択中のプロファイル index (0 origin)
static int g_current = 0;

// NVS handle
static Preferences g_prefs;

inline void loadCurrent() {
  g_prefs.begin("hokoro", true);   // read-only
  g_current = g_prefs.getInt("wifi_idx", 0);
  g_prefs.end();
  if (g_current < 0 || g_current >= COUNT) g_current = 0;
}

inline void saveCurrent(int idx) {
  g_prefs.begin("hokoro", false);  // read-write
  g_prefs.putInt("wifi_idx", idx);
  g_prefs.end();
  g_current = idx;
}

inline int current() { return g_current; }

inline const Profile& profile(int idx) { return g_profiles[idx]; }

// 単一プロファイルへの接続試行
inline bool tryConnect(int idx, uint32_t timeoutMs = 15000) {
  if (idx < 0 || idx >= COUNT) return false;
  if (g_profiles[idx].ssid == nullptr || strlen(g_profiles[idx].ssid) == 0) return false;

  Serial.printf("[wifi] trying profile #%d: %s\n", idx + 1, g_profiles[idx].ssid);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_profiles[idx].ssid, g_profiles[idx].pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] profile #%d connected, ip=%s rssi=%d\n",
                  idx + 1,
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    return true;
  }
  Serial.printf("[wifi] profile #%d FAILED\n", idx + 1);
  return false;
}

// 現在のプロファイルから順番に全プロファイルを試行 (自動フォールバック)。
// 成功したプロファイルを current として保存する。
inline bool connectAuto() {
  for (int offset = 0; offset < COUNT; offset++) {
    int idx = (g_current + offset) % COUNT;
    if (tryConnect(idx)) {
      if (idx != g_current) saveCurrent(idx);
      return true;
    }
  }
  Serial.println("[wifi] all profiles failed - running offline");
  return false;
}

}  // namespace wifip
