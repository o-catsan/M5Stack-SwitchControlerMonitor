# M5Stack Switch Controller Monitor

M5Stack に USB Host Shield を接続し、Nintendo Switch 用コントローラー (HORI PAD TURBO 等) の入力値をリアルタイムで LCD に表示するモニタリングツールです。

現在は `M5Stack Core2` を優先対象としており、`USB Host Shield Library 2.0` ベースで `M5Stack USB Module v1.2` を利用します。

## 機能

- **USB Host 接続**: M5Stack USB モジュール (MAX3421E) を介してコントローラーを認識
- **入力可視化**:
    - **ボタン**: A, B, X, Y, L, R, ZL, ZR, +, -, Home, Capture, Stick Click の押下状態を表示
    - **アナログスティック**: 左・右スティックの現在値を数値とグラフィックで表示
    - **十字キー (DPAD)**: 押されている方向 (UP, RIGHT, DOWN-LEFT 等) をテキストとビジュアルで表示
- **デバッグ情報**: 生の HID レポートデータ (Hex Dump) を表示
- **シリアル通信 (Serial2)**: ボードに応じて UART ピンを自動切替し、フォーマットされたコントローラー情報を 115200bps で送信 (200ms 間隔)
  - Core: `RX=GPIO16`, `TX=GPIO17`
  - Core2 (Port C): `RX=GPIO13`, `TX=GPIO14`

## 送信データフォーマット (Serial2)

**Serial2 (TX)** から送信されるデータは、以下の 7 バイトのカンマ区切り16進数文字列 + 改行コード (`\r\n`) です。
例: `00,00,00,80,80,80,80\r\n` (中立時)

| Byte | 内容 | 値の範囲・意味 |
|:---:|:---|:---|
| **0** | **Button 1** | ビットフラグ (A, B, X, Y, L, R, ZL, ZR) |
| **1** | **Button 2** | ビットフラグ (-, +, Home, Cap, LStick, RStick) |
| **2** | **DPAD** | **00:中立**, **01:上** .. **08:左上** (時計回り) |
| **3** | **Left Stick X** | 0x00(左) - 0xFF(右), 中心:0x80 |
| **4** | **Left Stick Y** | 0x00(上) - 0xFF(下), 中心:0x80 |
| **5** | **Right Stick X** | 0x00(左) - 0xFF(右), 中心:0x80 |
| **6** | **Right Stick Y** | 0x00(上) - 0xFF(下), 中心:0x80 |

### ボタン (Byte 0, Byte 1) ビット詳細

**Byte 0: Button 1**
| Bit | ボタン |
|:---:|:---|
| 0 | A |
| 1 | B |
| 2 | X |
| 3 | Y |
| 4 | L |
| 5 | R |
| 6 | ZL |
| 7 | ZR |

**Byte 1: Button 2**
| Bit | ボタン |
|:---:|:---|
| 0 | - (Minus) |
| 1 | + (Plus) |
| 2 | Home |
| 3 | Capture |
| 4 | L Stick (押し込み) |
| 5 | R Stick (押し込み) |
| 6 | (未使用) |
| 7 | (未使用) |


## 必要ハードウェア

- **M5Stack Core2**
- **M5Stack USB Module** (MAX3421E 搭載の USB Host Shield)
- **Nintendo Switch 対応 USB コントローラー** (動作確認済み: HORI PAD TURBO)
- **HORI PAD TURBO 本体の切替スイッチ**: `Switch 2` 側で使用（`PC` 側だと想定配列になりません）

### USB Module v1.2 の DIP スイッチ設定 (シルク準拠)

実機シルクの `PIN MAP` に合わせ、`SS Select(CH1-CH3)` と `INT Select(CH1-CH2)` を選択します。

| ターゲット | SPI(MOSI/MISO/SCK) | SS CH1 | SS CH2 | SS CH3 | INT CH1 | INT CH2 |
|:--|:--|:--:|:--:|:--:|:--:|:--:|
| Core | G23 / G19 / G18 | G13 | G5 | G0 | G35 | G34 |
| Core2 | G23 / G38 / G18 | G19 | G33 | G0 | G35 | G34 |

このプロジェクトでは、ビルド時に `SS` / `INT` のチャンネルを指定します。

- 既定値: `core2 + SS CH1 + INT CH1` (`GPIO19 / GPIO35`)
- 例 (`Core2`, `SS CH2`, `INT CH2`):
```powershell
.\build.ps1 -Board core2 -SsChannel 2 -IntChannel 2
```

## 開発環境 & 依存ライブラリ

ビルドには [Arduino CLI](https://arduino.github.io/arduino-cli/) を使用します。

### 依存ライブラリ (自動インストールされます)
- M5Unified
- USB Host Shield Library 2.0

`build.ps1` は `Documents/Arduino/libraries/USB_Host_Shield_Library_2.0` を事前チェックし、無ければインストール、あればそのライブラリに Core/Core2 の `SS/INT/SPI` ピン選択用パッチを自動適用してビルドします。

## 使い方

プロジェクトのルートディレクトリで `build.ps1` PowerShell スクリプトを実行します。

### ビルドと書き込み (Core2 / 自動検出)
```powershell
.\build.ps1
```

### ビルドのみ
```powershell
.\build.ps1 -SkipUpload
```

### ビルドと書き込み (COMポート指定)
```powershell
.\build.ps1 -Port COM5
```

### 旧 Core 向けにビルドする場合
```powershell
.\build.ps1 -Board core
```

## ライセンス

[MIT License](LICENSE)

Copyright (c) 2026 Noriki Nakamura
