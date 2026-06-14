# HANDOFF: Hokoro — Atom Matrix Office Status Indicator

**プロジェクト名**: `hokoro`（綻ろ）— 「綻ぶ＝蕾が開き始める」に由来。25ドットが点灯し状態が表に「ほころび出る」比喩。
**対象ハードウェア**: M5Stack Atom Matrix ESP32 Dev Kit v1.1 (SKU: C008-B-V11)
**作成日**: 2026-06-14 (JST)
**ライセンス（推奨）**: MIT
**開発環境**: PlatformIO (`platform = espressif32`, `board = m5stack-atom`, `framework = arduino`)

---

## 1. 目的・コンセプト

事務所の自席に据え置き、5×5 RGB LEDマトリクスで「いま在席して何をしているか」を遠目に伝えるステータスインジケータ。

- Wi-Fi経由でiCalフィードを取得し、進行中の予定があれば**残り時間をドット点灯量で表示**＋**予定種別（MTG）をスクロール表示**する。
- 昼食・離席時は本体ボタンで手動ステータス（**LUNCH / AWAY / BREAK**）に切り替え、戻ったらボタンで通常モードへ復帰する。

MTGなのか他タスク中なのか、離席中なのかを、PCの前まで来なくても判別できる状態の可視化を狙う。

---

## 2. ハードウェア諸元（一次情報）

| 項目 | パラメータ |
|------|-----------|
| SoC | ESP32-PICO-D4 デュアルコア 240MHz |
| SRAM / Flash | 520KB / 4MB |
| Wi-Fi | 2.4GHz（5GHz非対応） |
| ディスプレイ | 5×5＝25個 WS2812C-2020 RGB LED |
| 入力 | プログラマブルボタン ×1（マトリクス下部, G39） |
| その他 | BMI270 6軸IMU, IR送信, Grove(I2C+I/O+UART) |
| 給電 | 5V @ 500mA / USB Type-C（内蔵バッテリ無し） |
| サイズ / 重量 | 24.0×24.0×13.9mm / 7.3g |

> 出典: https://shop.m5stack.com/products/atom-matrix-esp32-dev-kit-v1-1 / https://docs.m5stack.com/en/products/sku/C008-B-V11

---

## 3. ファイル構成

```
hokoro/
├─ platformio.ini          ビルド設定
├─ .gitignore              secrets.h 等を除外
└─ src/
   ├─ main.cpp        (189行)  状態機械・ボタン処理・描画ループ
   ├─ display.h       ( 88行)  5×5描画ヘルパ（3×5フォント / スクロール / プログレス）
   ├─ ical.h          ( 85行)  .ics ストリーム取得＋直近予定の簡易抽出
   └─ secrets.example.h ( 17行) Wi-Fi/iCal URL テンプレ（→ secrets.h を作成）
```

---

## 4. 動作仕様

### 4.1 モード一覧

| モード | 表示内容 |
|--------|---------|
| NORMAL | 進行中の予定があれば「残量ドット ⇄ MTGスクロール」を3秒間隔でトグル。予定が無ければ消灯（待機）。 |
| MANUAL | 手動ステータス（LUNCH / AWAY / BREAK）の文字を横スクロール。 |

### 4.2 状態遷移

```
            短押し                短押し          短押し          短押し
[NORMAL] ──────────▶ [MANUAL:LUNCH] ─────▶ [MANUAL:AWAY] ─────▶ [MANUAL:BREAK] ──┐
   ▲                                                                              │
   └──────────────────────────────────────────────────────────────────────────┘
                         BREAK の次の短押しで NORMAL へ復帰（iCal即時再取得）
```

### 4.3 NORMAL モードの表示ロジック

- iCalを `ICAL_POLL_MS`（既定60秒）間隔でポーリングし、`fetchNext()` で「現在時刻以降に終了する最も近い予定」を1件取得。
- `now >= start` の予定が進行中のとき表示を有効化。
  - **残量ドット**: `lit = left * 25 / total` で 0〜25 ドットを青系点灯。
  - **MTGスクロール**: 「MTG」を橙赤でスクロール。
  - 上記を `TOGGLE_MS`（既定3秒）で交互表示。
- **超過時**（`left < 0`）: 最優先で全点灯・赤の点滅（`BLINK_MS` 既定500ms）。

### 4.4 色設計（CUD配慮）

| 表示 | 色 | 補助識別 |
|------|----|---------|
| MTG（予定種別） | 橙赤 0xC03000 | 「MTG」文字スクロール |
| LUNCH | 橙 0xA04000 | 「LUNCH」文字スクロール |
| AWAY | 紫 0x6010A0 | 「AWAY」文字スクロール |
| BREAK | 緑 0x108040 | 「BREAK」文字スクロール |
| 残量バー | 青系 0x104060 | 点灯ドット数 |
| 超過 | 赤 0xC00000 | 全点灯＋点滅 |

> 色のみに依存せず、必ず文字スクロール／点滅挙動と併用して識別できるようにしている。

---

## 5. 主要パラメータ（main.cpp 冒頭）

| 定数 | 既定値 | 意味 |
|------|--------|------|
| `ICAL_POLL_MS` | 60,000ms | iCal取得間隔 |
| `SCROLL_STEP_MS` | 120ms | スクロール1ドット移動間隔 |
| `TOGGLE_MS` | 3,000ms | 残量/MTG切替間隔 |
| `BLINK_MS` | 500ms | 超過点滅間隔 |

---

## 6. セットアップ手順

1. `src/secrets.example.h` を `src/secrets.h` にコピーし、`WIFI_SSID` / `WIFI_PASS` / `ICAL_URL` を記入。
2. iCal URLは Google カレンダーの「ical 形式の限定公開 URL」等を使用（`.ics`）。
3. `pio run` でビルド → `pio run -t upload` で書き込み。
4. `pio device monitor`（115200bps）でログ確認。

> `secrets.h` は `.gitignore` 済み。資格情報はコードに直書きしない。

---

## 7. 既知の制約・未実装（重要）

実機運用前にHANDOFF読者が認識すべき簡略化箇所。

1. **TLS証明書検証を省略**（`client.setInsecure()`）。常時運用では `setCACert()` でルート証明書固定を推奨。
2. **iCalパーサは簡易版**。`DTSTART` / `DTEND` / `SUMMARY` のみ抽出。以下は未対応:
   - 繰り返し予定（RRULE）
   - 終日予定（VALUE=DATE）
   - タイムゾーン定義（VTIMEZONE）の厳密処理 ※現状はUTC(`Z`)とローカルの二択近似
   - 複数 VEVENT 間の優先度（種別判定）
3. **ボタンは短押しのみ**（`M5.Btn.wasPressed()`）。長押し／ダブルクリックは未使用。
4. **未コンパイル検証**。C++構文・フォントテーブル・glyph探索は別途構文チェック済みだが、M5Atom実機ツールチェーンでのフルビルドは未実施。`pio run` での確認が必要。
5. **5×5の可読性は環境依存**。3×5フォントのスクロール判読性は環境光・距離で変わるため実機検証が必要。

---

## 8. フェーズ別実装計画

### Phase 0（完了）— 雛形
- PlatformIOプロジェクト構成、状態機械、ボタン巡回、残量ドット、MTG/手動スクロール。

### Phase 1 — 実機立ち上げ・基本動作確認
- [ ] `pio run` ビルド通過
- [ ] Wi-Fi接続・NTP同期確認
- [ ] iCal取得→直近予定の正しい抽出をシリアルログで確認
- [ ] 残量ドット・MTGスクロール・手動巡回の目視確認
- [ ] スクロール速度・トグル間隔の体感調整

### Phase 2 — iCalパーサ堅牢化
- [ ] タイムゾーン（VTIMEZONE / JST変換）の厳密化
- [ ] 終日予定・RRULEの基本対応
- [ ] SUMMARY からの種別判定（例: "MTG"/"会議" を含むか）で表示色・文字を切替

### Phase 3 — セキュリティ・運用性
- [ ] TLS証明書固定（`setCACert`）
- [ ] Wi-Fi切断時の再接続・フォールバック表示
- [ ] 設定値のNVS保存 or 簡易設定手段

### Phase 4 — UX拡張（任意）
- [ ] 手動ステータスに「経過時間」表示（LUNCH開始からの経過をドットで）
- [ ] 長押しによる即時NORMAL復帰
- [ ] 予定開始n分前の予告表示（点滅プリアラート）
- [ ] IMU（BMI270）でデバイスを伏せたら AWAY 自動化、等

---

## 9. 検証チェックリスト（実機）

- [ ] 予定進行中: 残量ドットが時間経過で減るか
- [ ] 予定進行中: MTGスクロールが正しく読めるか
- [ ] 予定超過: 赤点滅に移行するか
- [ ] 予定無し: 消灯（待機）するか
- [ ] ボタン: NORMAL→LUNCH→AWAY→BREAK→NORMAL の巡回が正しいか
- [ ] NORMAL復帰直後にiCalが即時再取得されるか
- [ ] 長時間連続稼働（数時間）でハングしないか

---

## 10. 参考リンク

- M5Stack 製品ページ: https://shop.m5stack.com/products/atom-matrix-esp32-dev-kit-v1-1
- 製品ドキュメント(C008-B-V11): https://docs.m5stack.com/en/products/sku/C008-B-V11
- M5Atom ライブラリ: https://github.com/m5stack/M5Atom
- PlatformIO board(m5stack-atom): https://docs.platformio.org/en/latest/boards/espressif32/m5stack-atom.html
- RFC 5545 (iCalendar): https://datatracker.ietf.org/doc/html/rfc5545
