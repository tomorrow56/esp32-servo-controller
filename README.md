# ESP32 Web APIサーボ制御システム

ESP32をWeb APIサーバーとして動作させ、ブラウザからスクリプトをアップロードしてサーボモーターを自動制御するシステムです。

## 📦 ファイル構成

```text
.
├── README.md                           # このファイル
├── web_api/                            # Wi-Fi Web API版
│   ├── src/
│   │   └── esp32_servo_webapi_server/
│   │       └── esp32_servo_webapi_server.ino
│   └── web_ui/
│       ├── script_controller_ui.html
│       ├── block_editor/
│       │   ├── index.html
│       │   └── app.js
│       └── sample_scripts/
└── serial_api/                         # USB Serial版（Web Serial API）
    ├── src/
    │   └── esp32_servo_serial/
    │       └── esp32_servo_serial.ino
    └── web_ui/
        ├── servo_control_webserial.html
        ├── servo_controller.js
        └── sample_scripts/
```

## 🚀 クイックスタート

### 1. 必要なもの

- **ハードウェア**: ESP32-WROOM-32, サーボモーター, 外部電源
- **ソフトウェア**: Arduino IDE, Wi-Fi環境

### 2. Arduino IDEのセットアップ

1. Arduino IDEをインストール
2. ESP32ボードサポートをインストール
3. ライブラリマネージャから `ESP32Servo` と `ArduinoJson` をインストール

### 3. ESP32への書き込み

1. `web_api/src/esp32_servo_webapi_server/esp32_servo_webapi_server.ino` を開き、Wi-FiのSSIDとパスワードを設定
2. ボードを `ESP32 Dev Module` に設定し、スケッチを書き込む
3. シリアルモニタでIPアドレスを確認

### 4. ハードウェアの接続

- サーボの信号線を指定のGPIOピンに接続
- **必ず外部電源を使用**し、ESP32とGNDを共通接続

### 5. Webインターフェースの起動

2種類のUIを用意しています。

**テキストエディタ** (`web_api/web_ui/script_controller_ui.html`)

1. ブラウザで開く
2. IPアドレスを入力し「接続テスト」
3. スクリプトをエディタに入力し「アップロード」→「実行」

**ブロックエディタ** (`web_api/web_ui/block_editor/index.html`)

1. ブラウザで開く
2. IPアドレスを入力し「接続テスト」
3. パレットからブロックをドラッグしてキャンバスに配置
4. 「アップロード」→「実行」

## 📝 スクリプト言語の基本

- `servo <channel> <angle>`: 個別サーボ制御
- `servos <ch1>:<angle1> ...`: 複数サーボ同時制御
- `wait <milliseconds>`: 待機

**例:**

```text
# サーボ0を90度に
servo 0 90

# 1秒待機
wait 1000

# サーボ0を0度に
servo 0 0
```

## 📖 詳細ドキュメント

完全なマニュアルは `WebAPI_Script_Manual.md` を参照してください。

## 📄 ライセンス

[MIT License](LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56
