#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// =====================================================================
//  schedule.h  —  Cloudflare Worker からの予定 JSON 取得
//
//  期待する JSON 形式 (Worker が KV から配信):
//    {
//      "updated_at": "2026-06-17T07:55:14Z",
//      "count": 11,
//      "events": [
//        { "start": 1781741700, "end": 1781742600 },
//        ...
//      ]
//    }
//
//  start / end は UTC epoch 秒。配列は start 昇順でソート済みである前提。
// =====================================================================

namespace schedule {

struct Event {
  time_t start = 0;
  time_t end   = 0;
};

static constexpr int MAX_EVENTS = 32;

struct List {
  bool  valid = false;
  int   count = 0;
  Event events[MAX_EVENTS];
};

inline List fetchAll(const char* url) {
  List result;
  WiFiClientSecure client;
  client.setInsecure();    // TLS 検証は省略。Phase 3 で setCACert へ
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("[sched] http.begin FAILED");
    return result;
  }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[sched] http status=%d (expected 200)\n", code);
    http.end();
    return result;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("[sched] json parse error: %s\n", err.c_str());
    return result;
  }

  JsonArray arr = doc["events"].as<JsonArray>();
  for (JsonObject ev : arr) {
    if (result.count >= MAX_EVENTS) break;
    result.events[result.count].start = (time_t)ev["start"].as<long>();
    result.events[result.count].end   = (time_t)ev["end"].as<long>();
    result.count++;
  }
  result.valid = true;
  Serial.printf("[sched] parsed %d events (updated_at=%s)\n",
                result.count,
                doc["updated_at"].as<const char*>() ? doc["updated_at"].as<const char*>() : "?");
  return result;
}

// 現在進行中 (start <= now <= end) の予定を返す。
// なければ終了から graceSec 以内の直近終了予定を返す (超過点滅用)。
// 該当無しなら start==0 のままの Event が返る。
//   dismissedEnd != 0 のとき、end がこれと一致する予定は active 選定から除外
//   (ユーザーの「終了確定」操作で dismiss された予定を二度と拾わないため)。
inline Event currentOrJustEnded(const List& list, time_t now,
                                bool& outIsOver,
                                time_t dismissedEnd = 0,
                                long graceSec = 5 * 60) {
  Event ret;
  outIsOver = false;
  time_t bestEnd = 0;
  for (int i = 0; i < list.count; i++) {
    const Event& e = list.events[i];
    if (dismissedEnd != 0 && e.end == dismissedEnd) continue;
    if (e.start <= now && now <= e.end) {
      outIsOver = false;
      return e;                       // in-progress を即座に返す
    }
    if (e.end < now && (now - e.end) <= graceSec) {
      if (e.end > bestEnd) {
        ret = e;
        bestEnd = e.end;
        outIsOver = true;
      }
    }
  }
  return ret;
}

} // namespace schedule
