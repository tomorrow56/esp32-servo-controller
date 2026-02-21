# ESP32 サーボ制御システム

ESP32を使ったブラウザベースのサーボモーター制御システムです。
Wi-Fi経由のWeb API版と、USBシリアル経由のWeb Serial API版の2種類を収録しています。

## �️ システム構成

| | web_api | serial_api |
| --- | --- | --- |
| **通信方式** | Wi-Fi（HTTP REST API） | USB シリアル（Web Serial API） |
| **対応ブラウザ** | 任意 | Chrome / Edge / Opera |
| **必要環境** | Wi-Fiルーター | USBケーブルのみ |
| **スクリプト実行** | ESP32側 | ブラウザ側 |
| **詳細** | [web_api/README.md](web_api/README.md) | [serial_api/README.md](serial_api/README.md) |

| | ros2 |
| --- | --- |
| **通信方式** | Wi-Fi（micro-ROS UDP + ROS2トピック） |
| **対応ブラウザ** | 任意 |
| **必要環境** | ROS2 Humble以降、micro-ROS Agent |
| **スクリプト実行** | script_runner_node（PC側） |
| **詳細** | [ros2/README.md](ros2/README.md) |

## �📦 ファイル構成

```text
.
├── README.md                               # このファイル
├── web_api/                                # Wi-Fi Web API版
│   ├── README.md
│   ├── WebAPI_Script_Manual.md
│   ├── src/
│   │   └── esp32_servo_webapi_server/
│   │       └── esp32_servo_webapi_server.ino
│   └── web_ui/
│       ├── script_controller_ui.html
│       ├── block_editor/
│       │   ├── index.html
│       │   └── app.js
│       └── sample_scripts/
├── serial_api/                             # USB Serial版（Web Serial API）
│   ├── README.md
│   ├── Servo_Script_Manual.md
│   ├── src/
│   │   └── esp32_servo_serial/
│   │       └── esp32_servo_serial.ino
│   └── web_ui/
│       ├── servo_control_webserial.html
│       ├── servo_controller.js
│       ├── block_editor/
│       │   ├── index.html
│       │   └── app.js
│       └── sample_scripts/
└── ros2/                                    # ROS2版（micro-ROS）
    ├── README.md
    ├── src/
    │   └── esp32_servo_microros/
    │       └── esp32_servo_microros.ino
    ├── ros2_package/
    │   └── servo_controller/
    │       ├── package.xml
    │       ├── CMakeLists.txt
    │       ├── launch/
    │       └── servo_controller/
    └── web_ui/
        └── script_controller_ros2.html
```

## 🚀 クイックスタート

### 共通: ハードウェアの準備

- **必要なもの**: ESP32-WROOM-32、サーボモーター（最大10個）、外部電源（5V / 3A以上）
- **GPIO接続**:

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

### Web API版（Wi-Fi）

1. `ESP32Servo` と `ArduinoJson` をArduino IDEにインストール
2. `web_api/src/esp32_servo_webapi_server/esp32_servo_webapi_server.ino` にSSID・パスワードを設定して書き込む
3. シリアルモニタでIPアドレスを確認
4. `web_api/web_ui/script_controller_ui.html` または `web_api/web_ui/block_editor/index.html` をブラウザで開く

詳細: [web_api/README.md](web_api/README.md)

### Serial API版（USB）

1. `ESP32Servo` をArduino IDEにインストール
2. `serial_api/src/esp32_servo_serial/esp32_servo_serial.ino` を書き込む
3. `serial_api/web_ui/servo_control_webserial.html` または `serial_api/web_ui/block_editor/index.html` をChrome/Edge/Operaで開く
4. 「ESP32に接続」ボタンでシリアルポートを選択

詳細: [serial_api/README.md](serial_api/README.md)

### ROS2版（micro-ROS）

1. `ESP32Servo` と micro-ROS for Arduino をArduino IDEにインストール
2. `ros2_package/servo_controller/` をROS2ワークスペースにコピーして `colcon build`
3. `ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888` でAgentを起動
4. `ros2/src/esp32_servo_microros/esp32_servo_microros.ino` にWi-Fi設定・Agent IPを設定して書き込む
5. `ros2 launch servo_controller servo_controller.launch.py` でノードを起動
6. `ros2/web_ui/script_controller_ros2.html` をブラウザで開く

詳細: [ros2/README.md](ros2/README.md)

## 📝 スクリプト言語の基本

両システム共通のコマンド:

```text
# 個別サーボ制御
servo 0 90

# 複数サーボ同時制御
servos 0:90 1:45 2:135

# 待機（ミリ秒）
wait 1000

# 条件分岐（Web API版はESP32側、Serial API版はブラウザ側で評価）
if servo0 == 90
  servo 0 0
endif
```

## 📄 ライセンス

[MIT License](LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56
