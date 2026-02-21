# ESP32 Web APIサーボ制御システム

ESP32をWeb APIサーバーとして動作させ、ブラウザからスクリプトをアップロードしてサーボモーターを自動制御するシステムです。

## 📦 ファイル構成

```
.
├── README.md                           # このファイル
├── WebAPI_Script_Manual.md             # 完全マニュアル（詳細仕様書）
├── esp32_webapi_script_server.ino      # ESP32用Arduinoスケッチ
├── script_controller_ui.html           # Webインターフェース（HTML/JS）
└── sample_scripts/                     # サンプルスクリプト集
    ├── wave_example.txt
    ├── sequence_example.txt
    └── dance_example.txt
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

1. `esp32_webapi_script_server.ino` を開き、Wi-FiのSSIDとパスワードを設定
2. ボードを `ESP32 Dev Module` に設定し、スケッチを書き込む
3. シリアルモニタでIPアドレスを確認

### 4. ハードウェアの接続

- サーボの信号線を指定のGPIOピンに接続
- **必ず外部電源を使用**し、ESP32とGNDを共通接続

### 5. Webインターフェースの起動

1. `script_controller_ui.html` をブラウザで開く
2. シリアルモニタで確認したIPアドレスを入力し、「接続テスト」
3. スクリプトをエディタに入力し、「アップロード」
4. 「実行」ボタンでスクリプトを開始

## 📝 スクリプト言語の基本

- `servo <channel> <angle>`: 個別サーボ制御
- `servos <ch1>:<angle1> ...`: 複数サーボ同時制御
- `wait <milliseconds>`: 待機

**例:**
```
# サーボ0を90度に
servo 0 90

# 1秒待機
wait 1000

# サーボ0を0度に
servo 0 0
```

## 📖 詳細ドキュメント

完全なマニュアルは `WebAPI_Script_Manual.md` を参照してください。

## 🤖 著者

Manus AI
