# ESP32 サーボ制御システム - Web API版

ESP32をWi-Fi経由のWeb APIサーバーとして動作させ、ブラウザからスクリプトをアップロードしてサーボモーターを自動制御するシステムです。

## 📦 ファイル構成

```text
.
├── README.md                               # このファイル
├── WebAPI_Script_Manual.md                 # 完全マニュアル（詳細仕様書）
├── src/
│   └── esp32_servo_webapi_server/
│       └── esp32_servo_webapi_server.ino   # ESP32用Arduinoスケッチ
└── web_ui/
    ├── script_controller_ui.html           # テキストエディタUI（HTML/JS）
    ├── block_editor/                       # ブロックエディタUI
    │   ├── index.html                      # ブロックエディタ本体
    │   └── app.js                          # ブロックエディタロジック
    └── sample_scripts/                     # サンプルスクリプト集
        ├── wave_example.txt                # 基本的な往復動作
        ├── sequence_example.txt            # 順番にサーボを動かす
        ├── dance_example.txt               # ダンス動作
        ├── robot_arm_example.txt           # ロボットアーム制御
        └── walking_robot_example.txt       # 4足歩行ロボット
```

## 🚀 クイックスタート

### 1. 必要なもの

- **ハードウェア**
  - ESP32-WROOM-32開発ボード
  - サーボモーター（最大10個）
  - 外部電源（5V、3A以上推奨）

- **ソフトウェア**
  - Arduino IDE
  - Wi-Fi環境（2.4GHz）
  - 任意のWebブラウザ

### 2. Arduino IDEのセットアップ

1. Arduino IDEをインストール
2. ESP32ボードサポートをインストール
3. ライブラリマネージャから以下をインストール
   - `ESP32Servo` by Kevin Harrington
   - `ArduinoJson` by Benoit Blanchon
   - `SimpleWiFiManager` by tomorrow56
   - `Adafruit SSD1306` by Adafruit
   - `Adafruit GFX Library` by Adafruit
   - `ESP32FwUploader` by tomorrow56

### 3. ESP32への書き込みとWi-Fi設定

1. `src/esp32_servo_webapi_server/esp32_servo_webapi_server.ino` をArduino IDEで開く
2. ボードを `ESP32 Dev Module` に設定
3. 適切なCOMポートを選択してスケッチを書き込む
4. **初回起動時のWi-Fi設定**
   - ESP32が `ESP32-ServoAP` というアクセスポイントを起動します
   - スマートフォンまたはPCで `ESP32-ServoAP` に接続
   - ブラウザで `http://192.168.4.1` を開く
   - Wi-FiのSSIDとパスワードを入力して保存
   - ESP32が再起動し、設定したWi-Fiに接続されます
5. シリアルモニタ（115200baud）またはOLEDでIPアドレスを確認

> **注意**: 設定済みWi-Fiが見つからない場合、5秒後に自動的にAPモードを終了して再起動します。
> Wi-Fi設定をリセットするには、IO0ボタン（BOOTボタン）を5秒間長押しします。

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

**テキストエディタ** (`web_ui/script_controller_ui.html`)

1. ブラウザで開く
2. ESP32のIPアドレスを入力し「接続テスト」
3. スクリプトをエディタに入力し「アップロード」→「実行」

**ブロックエディタ** (`web_ui/block_editor/index.html`)

1. ブラウザで開く
2. ESP32のIPアドレスを入力し「接続テスト」
3. パレットからブロックをドラッグしてキャンバスに配置
4. 「アップロード」→「実行」

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

### 条件分岐

```text
if servo0 == 90
  servo 0 0
  wait 500
else
  servo 0 90
endif
```

### ブロックエディタ専用（ブラウザ側で展開）

```text
# N回繰り返し
repeat 5 {
  servo 0 0
  wait 500
  servo 0 180
  wait 500
}

# 関数定義と呼び出し
function wave {
  servo 0 0
  wait 500
  servo 0 180
  wait 500
}
call wave
```

## 🌐 Web APIリファレンス（概要）

| メソッド | エンドポイント | 説明 |
| --- | --- | --- |
| POST | `/api/script/upload` | スクリプトをアップロード |
| POST | `/api/script/execute` | スクリプトを実行 |
| POST | `/api/script/stop` | 実行を停止 |
| GET | `/api/script/status` | 実行状態を取得 |
| POST | `/api/servo/{ch}` | 個別サーボを制御 |
| POST | `/api/servos` | 複数サーボを同時制御 |
| GET | `/api/servos` | 全サーボの角度を取得 |

詳細は `WebAPI_Script_Manual.md` を参照してください。

## 🔧 トラブルシューティング

### Wi-Fiに接続できない

- ESP32起動時に `ESP32-ServoAP` APが現れない場合、IO0ボタンを5秒長押しして設定リセット後、再度AP設定を行ってください
- ESP32がWi-Fiの電波範囲内にあるか確認してください
- 2.4GHz帯のWi-Fiのみ対応しています（5GHz非対応）

### Webページにアクセスできない

- PCとESP32が同じWi-Fiネットワークに接続されているか確認してください
- シリアルモニタで表示されたIPアドレスが正しいか確認してください

### スクリプトが実行されない

- 「実行」の前に「アップロード」が必要です
- 接続テストで接続済み状態を確認してください

## 📄 ライセンス

[MIT License](../LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56
