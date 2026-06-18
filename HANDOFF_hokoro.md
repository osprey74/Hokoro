# HANDOFF: Hokoro — Atom Matrix Office Status Indicator

**プロジェクト名**: `hokoro`（綻ろ）— 「綻ぶ＝蕾が開き始める」に由来。25ドットが点灯し状態が表に「ほころび出る」比喩。
**対象ハードウェア**: M5Stack Atom Matrix ESP32 Dev Kit v1.1 (SKU: C008-B-V11)
**初版作成日**: 2026-06-14 (JST)
**最終更新日**: 2026-06-17 (JST) — Phase 1〜4 大半の機能を実装完了
**ライセンス**: MIT
**開発環境**: PlatformIO (`platform = espressif32`, `board = m5stack-atom`, `framework = arduino`)

---

## 1. 目的・コンセプト

事務所の自席に据え置き、5×5 RGB LEDマトリクスで「いま在席して何をしているか」を遠目に伝えるステータスインジケータ。

- スケジュールバックエンドから進行中の予定があれば**残り時間をドット点灯量で表示**＋**「MTG」スクロール**で示す。
- **開始5分前**から予告: **増加ドット ⇄ 開始時刻「HH:MM」**のトグル表示。
- 昼食・離席時は本体ボタンで手動ステータス（**LUNCH / AWAY / BREAK**）に切替。
- 進行中／超過／予告中の予定を **ダブルクリックで「終了確定」dismiss** 可能。
- 複数 Wi-Fi プロファイル登録 + 起動時の自動フォールバック + 長押しで切替UI を備える。

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

## 3. アーキテクチャ概要

iCal 直接取得方式から、**Microsoft Graph API + Cloudflare Worker** 経由の中継方式へ移行済み。ESP32 側のパーサ複雑性が大幅に削減されている。

```
Outlook (M365 Exchange Online)
    ↓ Microsoft Graph API
    │  (RRULE 展開済 / TZ 正規化済 / Prefer: outlook.timezone="UTC")
[Cloudflare Worker  hokoro.<subdomain>.workers.dev]
    │ Cron 5分間隔で:
    │   - refresh_token (Cloudflare Secret) で access_token 更新
    │   - /me/calendarView 呼出 (向こう7日)
    │   - title / location / attendees / body は破棄
    │   - start, end (UTC epoch 整数) のみ KV に保存
    │   - 新しい refresh_token が返れば KV へ自動ローテーション
    │
    ↓ HTTPS GET (5分ポーリング)
Hokoro (ESP32)
    - ArduinoJson でパース
    - 状態判定 (進行中 / 5分前予告 / 超過 / idle) → 表示
```

→ ESP32 側に iCal の TZID / RRULE / EXDATE / VTIMEZONE 解析は不要。プライバシー上も **start/end の時刻情報のみ**が Cloudflare の経路に渡り、予定タイトルや出席者情報は中継されない。

---

## 4. ファイル構成

```
hokoro/
├─ platformio.ini              ビルド設定 (m5stack-atom / sim の2環境)
├─ wokwi.toml, diagram.json    Wokwi シミュレータ設定 (表示挙動の検証用)
├─ HANDOFF_hokoro.md           本ドキュメント
├─ src/
│  ├─ main.cpp                 状態機械・ボタン処理・描画ループ・iCal/JSON取得
│  ├─ display.h                5×5描画 + 可変幅(1〜5列)ピクセルフォント (29グリフ)
│  ├─ schedule.h               JSON 取得 + 現在/予告/超過判定
│  ├─ wifi_profiles.h          複数 Wi-Fi プロファイル + NVS 永続化 + 自動フォールバック
│  ├─ secrets.example.h        secrets.h のテンプレート
│  └─ (secrets.h)              実値ファイル (.gitignore 対象)
└─ worker/
   ├─ src/index.js             Cloudflare Worker 本体 (fetch & scheduled)
   ├─ wrangler.toml            Worker 設定 (KV / Cron)
   ├─ package.json
   ├─ package-lock.json
   └─ .gitignore
```

---

## 5. 動作仕様

### 5.1 モード一覧

| モード | 表示内容 |
|--------|---------|
| **NORMAL** | 状態に応じて以下のいずれか自動切替 |
| ├ 進行中 | 残量ドット(青) ⇄ 「MTG」スクロール(橙赤) を `TOGGLE_MS` でトグル |
| ├ 5分前予告 | 増加ドット ⇄ 開始時刻「HH:MM」スクロール (橙黄) |
| ├ 超過 | 25ドット全点灯・赤の点滅 (`BLINK_MS`=500ms) |
| └ 待機 | 消灯 |
| **MANUAL** | LUNCH / AWAY / BREAK の文字を色付きでスクロール |
| **WIFI_SELECT** | 上段ドットでプロファイル番号表示、シアン色 |
| **FONT_TEST** | 実装済グリフを1文字ずつ中央寄せ表示、紫色 (検証用) |

### 5.2 ボタン操作

| 操作 | 状態 | 動作 |
|---|---|---|
| 短押し | NORMAL | LUNCH へ (400ms 遅延発火, ダブルクリック検出窓のため) |
| 短押し | MANUAL | LUNCH→AWAY→BREAK→NORMAL 巡回 (即時) |
| 短押し | WIFI_SELECT | 候補プロファイル巡回 (即時) |
| 短押し | FONT_TEST | 次のグリフへスキップ (即時) |
| ダブルクリック | NORMAL (進行中/超過/予告中) | 該当予定を **dismiss** (end epoch を記憶し以降スキップ) |
| ダブルクリック | NORMAL idle | **FONT_TEST モードへ突入** |
| 長押し 2秒 | NORMAL / MANUAL | WIFI_SELECT モードへ |
| 長押し 1秒 | WIFI_SELECT | 候補プロファイルへ接続切替を確定 |
| 長押し 1秒 | FONT_TEST | NORMAL へ復帰 |

### 5.3 NORMAL モード内の状態判定優先順位

```
1. 進行中の予定 (start <= now <= end, dismiss 済除外)        → 残量ドット ⇄ MTG
2. 5分前予告  (0 < start - now <= 5分, dismiss 済除外)      → 増加ドット ⇄ 開始時刻
3. 超過       (end < now <= end + 5分 grace, dismiss 済除外) → 赤点滅
4. 待機                                                      → 消灯
```

**予告が超過より優先** ── 終わった会議より次の会議の準備を優先する設計。

### 5.4 Wi-Fi マルチプロファイル + 自動フォールバック

- `secrets.h` で `WIFI_SSID/_2/_3` を最大3つまで定義可能 (`#define` の有無で自動判定)
- 起動時、最後に成功したプロファイル番号 (NVS 永続化) から順に試行
- 全失敗時はオフラインモードで起動 (idle 表示のまま)
- 長押し 2秒で WIFI_SELECT モード → 候補プロファイル選択 → 長押し 1秒で確定

### 5.5 ダブルクリックによる終了確定 (dismiss)

進行中／超過／予告中の予定でダブルクリックすると、その予定の `end` epoch を記憶し、以降の状態判定で完全スキップする。

- 翌日の同名繰り返し予定は別 epoch のため影響なし
- RAM 保持。再起動でクリア (次回 poll で同じ予定が再検出される)

### 5.6 表示の向き

実機の物理設置向きに合わせて、`disp::put()` で **縦反転 (180°回転＋左右反転)** を実機ビルド時のみ適用。シミュレータビルド (`HOKORO_SIM`) では 180°回転 (Wokwi のチェイン方向差を吸収) を別途適用。

残量ドットの消去方向は **物理 top-left → 右** (見やすさ重視)。

### 5.7 色設計（CUD 配慮）

| 表示 | 色 | 補助識別 |
|------|----|---------|
| 進行中 (MTG) | 橙赤 `0xC03000` | 「MTG」スクロール |
| 残量バー | 青系 `0x104060` | 点灯ドット数 |
| 5分前予告 | 橙黄 `0xC07000` | 「HH:MM」スクロール ＋ 増加ドット |
| LUNCH | 橙 `0xA04000` | 「LUNCH」スクロール |
| AWAY | 紫 `0x6010A0` | 「AWAY」スクロール |
| BREAK | 緑 `0x108040` | 「BREAK」スクロール |
| 超過 | 赤 `0xC00000` | 全点灯＋点滅 |
| WIFI_SELECT | シアン `0x0080C0` | 上段ドット数で番号 |
| FONT_TEST | 紫 (AWAY 流用) | 中央寄せ1文字 |

色のみに依存せず、文字スクロール・点滅・ドット数で識別できるよう設計。

---

## 6. 主要パラメータ（main.cpp 冒頭）

| 定数 | 既定値 | 意味 |
|------|--------|------|
| `POLL_MS` | 60,000ms | スケジュール JSON 取得間隔 (1分) |
| `SCROLL_STEP_MS` | 200ms | スクロール1ドット移動間隔 |
| `TOGGLE_MS` | 10,000ms | 残量/スクロール切替間隔 |
| `BLINK_MS` | 500ms | 超過点滅間隔 |
| `LONGPRESS_MS` | 2,000ms | NORMAL/MANUAL→WIFI_SELECT 入りの長押し閾値 |
| `LONGPRESS_WS_MS` | 1,000ms | WIFI_SELECT / FONT_TEST 内での確定・退出長押し閾値 |
| `DOUBLE_CLICK_MS` | 400ms | ダブルクリック判定窓 |
| `PRE_WARN_SEC` | 300秒 (5分) | 予告開始タイミング |
| `FONT_TEST_MS` | 2,500ms | FONT_TEST モードの自動進み時間 |

---

## 7. セットアップ手順

### 7.1 ESP32 側

1. `src/secrets.example.h` を `src/secrets.h` にコピーし、以下を記入:
   - `WIFI_SSID` / `WIFI_PASS` (必須、2.4GHz)
   - `WIFI_SSID_2` / `WIFI_PASS_2` (任意、自動フォールバック用)
   - `WIFI_SSID_3` / `WIFI_PASS_3` (任意)
   - `SCHEDULE_URL`: Cloudflare Worker の JSON エンドポイント URL
2. `pio run -e m5stack-atom -t upload --upload-port COMx` で書き込み
3. `pio device monitor --port COMx` でログ確認 (115200bps)

### 7.2 Cloudflare Worker 側

`worker/` ディレクトリで:

```
npm install
npx wrangler login
npx wrangler kv namespace create SCHEDULE
# → 表示された id を wrangler.toml の [[kv_namespaces]] に貼る

npx wrangler secret put TENANT_ID
npx wrangler secret put CLIENT_ID
npx wrangler secret put CLIENT_SECRET
npx wrangler secret put REFRESH_TOKEN
npx wrangler secret put REFRESH_KEY        # 任意 (手動 /refresh エンドポイント用)

npx wrangler deploy
```

動作確認: `curl https://hokoro.<subdomain>.workers.dev/schedule.json` で events を含む JSON が返ること。

### 7.3 Microsoft Entra アプリ登録 (refresh_token 取得)

1. Entra 管理センター ([entra.microsoft.com](https://entra.microsoft.com)) → アプリの登録 → 新規登録 (Single tenant)
2. 「API のアクセス許可」で **委任されたアクセス許可** を追加: `Calendars.Read`, `offline_access`, `User.Read`
3. 「管理者の同意を与えます」を実行
4. 「証明書とシークレット」でクライアントシークレットを作成し **値** を控える (再表示不可)
5. 「認証」→ プラットフォーム追加 → Web → リダイレクト URI に `http://localhost:8080/callback` を登録
6. ローカルで Node.js スクリプト ([g:\tmp\graph-auth\get-refresh-token.mjs](g:\tmp\graph-auth\get-refresh-token.mjs)) を実行し、ブラウザ認可フローで `refresh_token` を取得
7. その値を 7.2 の `REFRESH_TOKEN` シークレットに設定

### 7.4 シミュレータ (Wokwi)

`pio run -e sim` でビルドし、`F1 → Wokwi: Start Simulator` で起動。Wi-Fi/取得はスキップされ、60秒ダミーMTG が注入されるため表示挙動だけ検証可能。

---

## 8. 既知の制約・未実装

初版で挙げていた制約の大半は実装が進んだ結果解消済み。残るものは以下。

1. **TLS 証明書検証を省略** (`setInsecure()`): Phase 3 残作業として Cloudflare 側の CA 固定 (`setCACert()`) 推奨
2. **Wi-Fi 切断時の自動再接続なし**: 起動時のフォールバックのみ。実行中の切断はリセットが必要
3. **NTP 同期は起動時のみ**: 長期稼働でドリフトの可能性あり (Worker 側に時刻管理を依存しているため実害は小さい)
4. **5×5 の可読性は環境依存**: 現状のフォントは 1〜5列の可変幅、29 グリフ実装済。検証は `FONT_TEST` モードで可能

---

## 9. フェーズ別進捗

### Phase 0 — 雛形 (完了)
PlatformIO 構成、状態機械、ボタン巡回、残量ドット、MTG/手動スクロール

### Phase 1 — 実機立ち上げ・基本動作確認 (完了)
- [x] `pio run` ビルド通過
- [x] Wi-Fi 接続 + NTP 同期確認
- [x] スケジュール取得 (Worker JSON) 確認
- [x] 残量ドット / MTG スクロール / 手動巡回の実機目視確認
- [x] スクロール速度・トグル間隔の体感調整

### Phase 2 — iCal パーサ堅牢化 (Worker 化により解消)
- [x] TZID / RRULE / 終日予定: Microsoft Graph API 側で展開済 (ESP32 側不要)
- [-] SUMMARY からの種別判定: タイトルを ESP32 に送らない設計のため不採用

### Phase 3 — セキュリティ・運用性 (大半完了)
- [ ] TLS 証明書 pin 化 (要: 実機での証明書チェーン取得とビルド構成変更)
- [x] Wi-Fi マルチプロファイル + 自動フォールバック
- [x] 実行中の Wi-Fi 切断検知・再接続 (30秒間隔監視)
- [x] 設定値の NVS 保存 (Wi-Fi プロファイル番号)

### Phase 4 — UX 拡張 (大半完了)
- [x] 5分前予告 (増加ドット ⇄ 開始時刻トグル表示)
- [x] ダブルクリックで終了確定 (dismiss)
- [x] フォント拡充 (数字 / 記号 / 4×5・5×5 可変幅)
- [x] FONT_TEST モード (検証用)
- [x] 経過時間表示 (LUNCH 等の手動ステータスに、1ドット=1分の増加)
- [ ] 長押しによる即時 NORMAL 復帰 (現状は BREAK の次のボタンで復帰)
- [ ] IMU (BMI270) でデバイスを伏せたら AWAY 自動化 等

---

## 10. 検証チェックリスト

- [x] 予定進行中: 残量ドットが時間経過で減るか (top-left → 右に消える)
- [x] 予定進行中: MTG スクロールが正しい向きで読めるか
- [x] 5分前予告: 増加ドット ⇄ 開始時刻スクロールが交互表示されるか
- [x] 予定超過: 赤点滅に移行するか
- [x] 予定無し: 消灯 (待機) するか
- [x] ボタン: NORMAL→LUNCH→AWAY→BREAK→NORMAL の巡回が正しいか
- [x] ボタン: ダブルクリックで進行中／超過の予定が dismiss されるか
- [x] ボタン: 長押し 2秒で WIFI_SELECT 突入、プロファイル切替確定
- [x] FONT_TEST: 全グリフが順次表示されるか
- [x] 長時間連続稼働 (数時間〜一晩) でハングしないか
- [ ] Wi-Fi 切断・再接続時の挙動 (実装済、実機検証待ち)

---

## 11. 参考リンク

- M5Stack Atom Matrix 製品ページ: https://shop.m5stack.com/products/atom-matrix-esp32-dev-kit-v1-1
- 製品ドキュメント(C008-B-V11): https://docs.m5stack.com/en/products/sku/C008-B-V11
- M5Atom ライブラリ: https://github.com/m5stack/M5Atom
- PlatformIO board(m5stack-atom): https://docs.platformio.org/en/latest/boards/espressif32/m5stack-atom.html
- ArduinoJson: https://arduinojson.org/
- Microsoft Graph API (calendarView): https://learn.microsoft.com/en-us/graph/api/user-list-calendarview
- Microsoft Entra (アプリ登録): https://entra.microsoft.com
- Cloudflare Workers: https://developers.cloudflare.com/workers/
- Cloudflare Workers KV: https://developers.cloudflare.com/kv/
- Cloudflare Workers Cron Triggers: https://developers.cloudflare.com/workers/configuration/cron-triggers/
- RFC 5545 (iCalendar, 参考): https://datatracker.ietf.org/doc/html/rfc5545
