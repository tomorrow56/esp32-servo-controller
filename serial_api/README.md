# ESP32 サーボ制御システム - Web Serial版

ESP32とWeb Serial APIを使用した、ブラウザベースのサーボモーター制御システムです。

## 📦 ファイル構成

```text
.
├── README.md                           # このファイル
├── Servo_Script_Manual.md              # 完全マニュアル（詳細仕様書）
├── src/
│   └── esp32_servo_serial/
│       └── esp32_servo_serial.ino      # ESP32用Arduinoスケッチ
└── web_ui/
    ├── servo_control_webserial.html    # テキストエディタUI（HTML）
    ├── servo_controller.js             # テキストエディタUI（JavaScript）
    ├── block_editor/                   # ブロックエディタUI
    │   ├── index.html              # ブロックエディタ本体
    │   └── app.js                  # ブロックエディタロジック
    └── sample_scripts/                 # サンプルスクリプト集
        ├── wave_example.txt            # 基本的な往復動作
        ├── robot_arm_example.txt       # ロボットアーム制御
        ├── dance_example.txt           # ダンス動作
        └── walking_robot_example.txt   # 4足歩行ロボット
```

## 🚀 クイックスタート

### 1. 必要なもの

- **ハードウェア**
  - ESP32-WROOM-32開発ボード
  - サーボモーター（最大10個）
  - 外部電源（5V、3A以上推奨）
  - USBケーブル

- **ソフトウェア**
  - Arduino IDE
  - Chrome/Edge/Opera（Web Serial API対応ブラウザ）

### 2. Arduino IDEのセットアップ

1. Arduino IDEをインストール
2. ESP32ボードサポートをインストール
3. ライブラリマネージャから `ESP32Servo` をインストール

### 3. ESP32への書き込み

1. `src/esp32_servo_serial/esp32_servo_serial.ino` をArduino IDEで開く
2. ボードを `ESP32 Dev Module` に設定
3. 適切なCOMポートを選択
4. スケッチを書き込む

### 4. ハードウェアの接続

| サーボ | GPIO | ボード表記 |
| --- | --- | --- |
| 0 | 23 | IO23 |
| 1 | 19 | IO19 |
| 2 | 18 | IO18 |
| 3 | 5 | IO5 |
| 4 | 17 | IO17 |
| 5 | 16 | IO16 |
| 6 | 4 | IO4 |
| 7 | 27 | IO27 |
| 8 | 14 | IO14 |
| 9 | 12 | IO12 |

**重要**: 必ず外部電源を使用し、ESP32のGNDと外部電源のGNDを共通接続してください。

### 5. Webインターフェースの起動

2種類のUIを用意しています。

**テキストエディタ** (`web_ui/servo_control_webserial.html`)

1. ブラウザで開く
2. 「ESP32に接続」ボタンをクリックしてシリアルポートを選択
3. スクリプトを入力または読み込んで実行

**ブロックエディタ** (`web_ui/block_editor/index.html`)

1. ブラウザで開く（Chrome / Edge / Opera必須）
2. 「ESP32に接続」ボタンをクリックしてシリアルポートを選択
3. パレットからブロックをドラッグしてキャンバスに配置
4. 「実行」ボタンでスクリプトをブラウザ側で実行（コマンドを順次シリアル送信）

## 📝 スクリプト言語の基本

### 基本コマンド

```text
# 個別サーボ制御
servo 0 90

# 複数サーボ同時制御
servos 0:90 1:45 2:135

# 待機（ミリ秒）
wait 1000
```

### ループ

```text
# 5回繰り返し
repeat 5 {
  servo 0 180
  wait 500
  servo 0 0
  wait 500
}

# 無限ループ
loop {
  servo 0 90
  wait 1000
}
```

### 関数

```text
# 関数定義
function wave {
  servo 0 0
  wait 500
  servo 0 180
  wait 500
}

# パラメータ付き関数
function move_to angle {
  servo 0 $1
  wait 1000
}

# 関数呼び出し
call wave
call move_to 90
```

## 📖 詳細ドキュメント

完全なマニュアルは `Servo_Script_Manual.md` を参照してください。以下の内容が含まれています：

- システム構成の詳細
- セットアップ手順
- スクリプト言語の完全仕様
- コマンドリファレンス
- サンプルスクリプト集
- トラブルシューティング
- 技術詳細

## 🎯 サンプルスクリプト

`sample_scripts/` フォルダには、以下のサンプルが含まれています：

- **wave_example.txt**: 基本的な往復動作
- **robot_arm_example.txt**: ロボットアームの制御
- **dance_example.txt**: ダンス動作
- **walking_robot_example.txt**: 4足歩行ロボット

これらのファイルをWebインターフェースから読み込んで、すぐに試すことができます。

## 🔧 トラブルシューティング

### 接続できない

- Web Serial API対応ブラウザ（Chrome/Edge/Opera）を使用していますか？
- USBケーブルがデータ通信対応ですか？
- 他のアプリケーション（Arduino IDEなど）がシリアルポートを使用していませんか？

### サーボが動かない

- 外部電源は接続されていますか？
- ESP32のGNDと外部電源のGNDは共通接続されていますか？
- 信号線は正しいGPIOピンに接続されていますか？

詳細は `Servo_Script_Manual.md` のトラブルシューティングセクションを参照してください。

## 📄 ライセンス

[MIT License](../LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56

---

**注意**: サーボモーターを動かす際は、周囲の安全を確保してください。電源容量が不足すると、ESP32やサーボが故障する可能性があります。
