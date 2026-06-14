#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// =====================================================================
//  ical.h  —  iCal フィードの取得と「直近1件」の簡易抽出
//  ※ 完全な RFC5545 パーサではない。DTSTART/DTEND/SUMMARY のみ抽出。
//     繰り返し予定(RRULE)・終日予定・複数 VEVENT の優先度判定は
//     HANDOFF の Phase2 以降で拡張する想定。
// =====================================================================

namespace ical {

struct Event {
  bool   valid = false;
  time_t start = 0;
  time_t end   = 0;
  String summary;
};

// "YYYYMMDDTHHMMSSZ"(UTC) または "YYYYMMDDTHHMMSS"(localと仮定) を time_t へ
inline time_t parseDt(const String& v) {
  if (v.length() < 15) return 0;
  struct tm t = {};
  t.tm_year = v.substring(0, 4).toInt() - 1900;
  t.tm_mon  = v.substring(4, 6).toInt() - 1;
  t.tm_mday = v.substring(6, 8).toInt();
  t.tm_hour = v.substring(9, 11).toInt();
  t.tm_min  = v.substring(11, 13).toInt();
  t.tm_sec  = v.substring(13, 15).toInt();
  bool isUtc = v.endsWith("Z");
  time_t e = mktime(&t);              // mktime はローカルTZ基準
  if (isUtc) e -= TZ_OFFSET_SEC;      // UTC表記なら local 補正を打ち消す
  return e;
}

// ストリーム読みで VEVENT を走査し、現在時刻以降で最も近い予定を返す。
// （メモリ節約のため全文をバッファしない）
inline Event fetchNext(const char* url, time_t now) {
  Event best;
  WiFiClientSecure client;
  client.setInsecure();   // 注意: 証明書検証を省略。運用では setCACert 推奨
  HTTPClient http;
  if (!http.begin(client, url)) return best;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return best; }

  WiFiClient* stream = http.getStreamPtr();
  Event cur; bool inEvent = false;
  String line;

  auto consider = [&](Event& ev) {
    if (!ev.valid) return;
    // 進行中 or これから始まる最も近い予定を選ぶ
    if (ev.end >= now) {
      if (!best.valid || ev.start < best.start) best = ev;
    }
  };

  while (http.connected() && stream->available()) {
    line = stream->readStringUntil('\n');
    line.trim();
    if (line == "BEGIN:VEVENT") { cur = Event(); inEvent = true; }
    else if (line == "END:VEVENT") {
      cur.valid = (cur.start != 0);
      consider(cur);
      inEvent = false;
    } else if (inEvent) {
      if (line.startsWith("DTSTART")) {
        int p = line.indexOf(':'); if (p >= 0) cur.start = parseDt(line.substring(p + 1));
      } else if (line.startsWith("DTEND")) {
        int p = line.indexOf(':'); if (p >= 0) cur.end = parseDt(line.substring(p + 1));
      } else if (line.startsWith("SUMMARY")) {
        int p = line.indexOf(':'); if (p >= 0) cur.summary = line.substring(p + 1);
      }
    }
    yield();
  }
  http.end();
  return best;
}

} // namespace ical
