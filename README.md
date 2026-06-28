# M5Stack Switch Controller to CoRE Wireless Sender

M5Stack に USB Host Shield を接続し、Nintendo Switch 用コントローラー (HORI PAD TURBO 等) の入力値を CoRE で支給される無線送信モジュール向けに変換・送信するプログラムです。

`M5Stack Core`、`M5Stack Core2`、`M5 CoreS3 SE` を対象に、`USB Host Shield Library 2.0` ベースで `M5Stack USB Module v1.2` を利用します。
UI/電源制御は `M5Unified` 前提で実装しており、`M5Stack.h` ではなく `M5Unified.h` を使用します。

## 機能

- **USB Host 接続**: M5Stack USB モジュール (MAX3421E) を介してコントローラーを認識
- **入力可視化**:
    - **ボタン**: A, B, X, Y, L, R, ZL, ZR, +, -, Home, Capture, Stick Click の押下状態を表示
    - **アナログスティック**: 左・右スティックの現在値を数値とグラフィックで表示
    - **十字キー (DPAD)**: 押されている方向 (UP, RIGHT, DOWN-LEFT 等) をテキストとビジュアルで表示
- **デバッグ情報**: 生の HID レポートデータ (Hex Dump) を表示
- **バッテリ残量表示**: 画面最下段の送信データ右側に `BAT: **%` 形式で M5Unified の参考残量を表示
  - 51%以上: 白色
  - 26〜50%: 黄色
  - 25%以下: 赤色
  - M5Stack Basic / Fire / M5GO系では、ハードウェア制約により 0%、25%、50%、75%、100% の5段階表示になる場合があります。
  - 残量は `M5.Power.getBatteryLevel()` による参考値であり、厳密な残量計ではありません。
- **シリアル通信 (Serial2)**: ボードに応じて UART ピンを自動切替し、フォーマットされたコントローラー情報を 115200bps で送信 (200ms 間隔)
  - Core: `RX=GPIO16`, `TX=GPIO17`
  - Core2 (Port C): `RX=GPIO13`, `TX=GPIO14`
  - CoreS3 SE (Port C): `RX=GPIO18`, `TX=GPIO17`

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

- **M5Stack Core / Core2 / CoreS3 SE**
- **M5Stack USB Module** (MAX3421E 搭載の USB Host Shield)
- **Nintendo Switch 対応 USB コントローラー** (動作確認済み: HORI PAD TURBO)
- **HORI PAD TURBO 本体の切替スイッチ**: `Switch 2` 側で使用（`PC` 側だと想定配列になりません）

### USB Module v1.2 の DIP スイッチ設定 (シルク準拠)

実機シルクの `PIN MAP` に合わせ、`SS Select(CH1-CH3)` と `INT Select(CH1-CH2)` を選択します。

| 信号 (Signal) | チャンネル | Core (GPIO) | Core2 (GPIO) | 設定値 | 備考 |
|:---|:---:|:---:|:---:|:---:|:---|
| **SS** (Slave Select) | **CH1** | **G13** | **G19** | **ON** | デフォルトの選択信号 |
| | CH2 | G5 | G33 | OFF | CoreではmicroSDと競合するため |
| | CH3 | G0 | G0 | OFF | 未使用 |
| **INT** (Interrupt) | **CH1** | **G35** | **G35** | **ON** | デフォルトの通知信号 |
| | CH2 | G34 | G34 | OFF | 未使用 |

**CoreS3 SE は USB Module v1.2 の SS / INT を必ず両方 CH2 に設定してください。** USB Host Shield Library 2.0 PR #843 のCoreS3実装に従い、SS=GPIO1、INT=GPIO14、SPI=`SCK=GPIO36/MOSI=GPIO37/MISO=GPIO35` を使用します。CoreS3 SE のCH1/CH3 GPIOは確認済みではないため、このターゲットでは選択できません。

## ビルド済みバイナリを使う（コンパイル不要）

コンパイル環境を用意しなくても、GitHub Releases からダウンロードしたビルド済みバイナリを書き込めます。

### 必要なもの
- [Arduino CLI](https://arduino.github.io/arduino-cli/installation/) のインストール
- M5Stack を PC に接続

### 手順

1. [Releases](../../releases) から M5Stack のボードに合う `.bin` をダウンロードする

   | ファイル名 | 対象ボード |
   |---|---|
   | `SwitchSender_core2_SS-CH1_INT-CH1.bin` | M5Stack Core2 (デフォルト) |
   | `SwitchSender_core_SS-CH1_INT-CH1.bin` | M5Stack Core |
   | `SwitchSender_cores3se_SS-CH2_INT-CH2.bin` | M5 CoreS3 SE |

2. ダウンロードした `.bin` を任意のフォルダに置く（例: `C:\Downloads\SwitchSender\`）

3. M5Stack を PC に接続し、対象ボードを明示して `flash.ps1` を実行する

```powershell
# build/ に .bin がある場合（build.ps1 -ExportBinaries 実行後）
.\flash.ps1 -Board core -Port COMx
.\flash.ps1 -Board core2 -Port COMx
.\flash.ps1 -Board cores3se -Port COM9

# ダウンロードした Arduino CLI のバイナリ一式を指定して書き込む場合
.\flash.ps1 -Board cores3se -Port COM9 -BinDir C:\Downloads\SwitchSender
```

`build` ディレクトリに複数ボードの成果物が存在する場合、誤書き込み防止のため `flash.ps1` は `-Board` 未指定をエラーにします。書き込み時は対象ボードを明示してください。

---

## 開発環境 & 依存ライブラリ

ビルドには [Arduino CLI](https://arduino.github.io/arduino-cli/) を使用します。

### 依存ライブラリ (自動インストールされます)
- M5Unified
- USB Host Shield Library 2.0

`build.ps1` は `Documents/Arduino/libraries/USB_Host_Shield_Library_2.0` を事前チェックし、無ければインストールします。古いライブラリにのみ Core/Core2 のピン別名と、USB Host Shield Library 2.0 PR #843 相当のCoreS3定義を冪等に追加します。upstream に定義済みなら追加変更しません。

CoreS3 SE は `m5stack:esp32` core **3.3.7** でビルド・実機動作を確認しています。

## 環境設定

設定ファイル `config.json` を作成することで、環境ごとのデフォルト値を固定できます。
リポジトリにある `config.json.sample` を `config.json` にコピーして編集してください。

```powershell
cp config.json.sample config.json
```

### 設定項目

- `Board`: デフォルトのターゲットボードを指定します (`core`、`core2`、`cores3se`)。
- `ArduinoDir`: Arduino ライブラリのルートパスを指定します。未指定の場合は `$HOME/Documents/Arduino` が使用されます。

### 優先順位

1. `build.ps1` 実行時の引数 (例: `-Board core`)
2. `config.json` 内の設定
3. `build.ps1` 内の既定値

## 使い方
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

### バイナリを build/ フォルダに出力する
```powershell
.\build.ps1 -ExportBinaries
```
`build/` フォルダに `.bin` が生成されます。書き込みはスキップされます。

### 旧 Core 向けにビルドする場合
```powershell
.\build.ps1 -Board core
```

### CoreS3 SE 向けにビルドする場合

```powershell
# SS/INT は、指定しない限り双方 CH2 になる
.\build.ps1 -Board cores3se -SkipUpload

# DIP設定を含むリリース用ファイルを出力
.\build.ps1 -Board cores3se -SsChannel 2 -IntChannel 2 -ExportBinaries
```

## ライセンス

[MIT License](LICENSE)

Copyright (c) 2026 Noriki Nakamura
