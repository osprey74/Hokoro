# Hokoro（綻ろ）

> Atom Matrix Office Status Indicator — 5×5 の LED ドットで「いま在席して何をしているか」を遠目に伝える、自席据え置きステータスインジケータ。

**Hokoro（綻ろ）** は「綻ぶ＝蕾が開き始める」に由来します。25 個の LED ドットが点灯し、予定の進行や離席状態が表に「ほころび出る」── そんな比喩を名前に込めています。

---

## できること

- **予定の可視化**: Wi-Fi 経由で iCal フィードを取得し、進行中の予定があれば**残り時間をドット点灯量で表示**＋**予定種別（MTG）をスクロール表示**します。
- **離席ステータスの手動切替**: 本体ボタンで **LUNCH / AWAY / BREAK** を巡回表示。戻ったらボタンで通常モードへ復帰します。
- MTG 中なのか、他タスク中なのか、離席中なのかを、PC の前まで来なくても判別できます。

---

## 対象ハードウェア

[M5Stack Atom Matrix ESP32 Dev Kit v1.1](https://shop.m5stack.com/products/atom-matrix-esp32-dev-kit-v1-1)（SKU: C008-B-V11）

| 項目 | 仕様 |
|------|------|
| SoC | ESP32-PICO-D4 / 240MHz / SRAM 520KB / Flash 4MB |
| Wi-Fi | 2.4GHz（5GHz 非対応） |
| ディスプレイ | 5×5＝25 個 WS2812C-2020 RGB LED |
| 入力 | プログラマブルボタン ×1 |
| 給電 | 5V / USB Type-C（内蔵バッテリ無し） |

---

## モードと操作

| モード | 表示 |
|--------|------|
| NORMAL | 進行中の予定があれば「残量ドット ⇄ MTG スクロール」を 3 秒間隔でトグル。予定が無ければ消灯（待機）。 |
| MANUAL | 手動ステータス（LUNCH / AWAY / BREAK）を横スクロール。 |

```
            短押し              短押し          短押し          短押し
[NORMAL] ────────▶ [LUNCH] ──────▶ [AWAY] ──────▶ [BREAK] ──┐
   ▲                                                         │
   └─────────────────────────────────────────────────────────┘
                  BREAK の次の短押しで NORMAL へ復帰
```

### 色設計（Color Universal Design 配慮）

色のみに依存せず、必ず文字スクロール／点滅挙動と併用して識別できるようにしています。

| 表示 | 色 | 補助識別 |
|------|----|---------|
| MTG | 橙赤 | 「MTG」文字 |
| LUNCH | 橙 | 「LUNCH」文字 |
| AWAY | 紫 | 「AWAY」文字 |
| BREAK | 緑 | 「BREAK」文字 |
| 残量バー | 青系 | 点灯ドット数 |
| 超過 | 赤 | 全点灯＋点滅 |

---

## セットアップ

1. `src/secrets.example.h` を `src/secrets.h` にコピーし、各値を記入します。

   ```cpp
   #define WIFI_SSID  "YOUR_WIFI_SSID"
   #define WIFI_PASS  "YOUR_WIFI_PASSWORD"
   #define ICAL_URL   "https://example.com/basic.ics"
   ```

   > iCal URL は Google カレンダーの「ical 形式の限定公開 URL」等（`.ics`）を使用します。
   > `secrets.h` は `.gitignore` 済み。資格情報はコミットしないでください。

2. ビルド・書き込み（[PlatformIO](https://platformio.org/)）

   ```sh
   pio run                 # ビルド
   pio run -t upload       # 書き込み
   pio device monitor      # シリアルログ（115200bps）
   ```

---

## ディレクトリ構成

```
hokoro/
├─ platformio.ini          ビルド設定（board = m5stack-atom）
├─ README.md               本ファイル
├─ LICENSE                 MIT
├─ HANDOFF_hokoro.md       設計・実装計画・検証チェックリスト
├─ .gitignore              secrets.h 等を除外
└─ src/
   ├─ main.cpp             状態機械・ボタン処理・描画ループ
   ├─ display.h            5×5 描画ヘルパ（3×5 フォント / スクロール / プログレス）
   ├─ ical.h               .ics ストリーム取得＋直近予定の簡易抽出
   └─ secrets.example.h    設定テンプレ（→ secrets.h を作成）
```

---

## 開発状況

Phase 0（雛形）完了。実装計画と既知の制約は [`HANDOFF_hokoro.md`](./HANDOFF_hokoro.md) を参照してください。実機ビルド・iCal パーサ堅牢化・TLS 証明書固定は Phase 1 以降の対象です。

---

## ライセンス

[MIT](./LICENSE) © 2026 osprey74
