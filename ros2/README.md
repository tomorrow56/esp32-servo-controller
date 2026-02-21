# ESP32 サーボ制御システム - ROS2版

ESP32上でmicro-ROSを動作させ、ROS2トピック経由でサーボモーターを制御するシステムです。
`web_api` 版と同じHTTP APIインターフェースを維持しながら、バックエンドをROS2に置き換えています。

## 📦 ファイル構成

```text
.
├── README.md                                   # このファイル
├── src/
│   └── esp32_servo_microros/
│       └── esp32_servo_microros.ino            # ESP32用 micro-ROSスケッチ
├── ros2_package/
│   └── servo_controller/                       # ROS2パッケージ
│       ├── package.xml
│       ├── CMakeLists.txt
│       ├── launch/
│       │   └── servo_controller.launch.py      # 起動ファイル
│       └── servo_controller/
│           ├── __init__.py
│           ├── servo_bridge_node.py            # サーボ制御ブリッジノード（port 8080）
│           └── script_runner_node.py           # スクリプト実行ノード（port 8081）
└── web_ui/
    ├── script_controller_ros2.html             # WebインターフェースUI（テキスト）
    └── block_editor/
        ├── index.html                          # ブロックエディタUI
        └── app.js                              # ブロックエディタロジック
```

## 🏗️ システム構成

```text
ブラウザ (Web UI)
    │  HTTP REST API
    ▼
servo_bridge_node (port 8080)   script_runner_node (port 8081)
    │  /servo_command                │  /servo_command
    │  /servos_command               │  /servos_command
    └──────────────┬─────────────────┘
                   │ ROS2 Topics (std_msgs/Int32MultiArray)
                   ▼
         ESP32 (micro-ROS node)
           esp32_servo_node
                   │
                   ▼
         サーボモーター × 最大10個
```

## 🚀 クイックスタート

### 1. 必要なもの

- **ハードウェア**
  - ESP32-WROOM-32開発ボード
  - サーボモーター（最大10個）
  - 外部電源（5V / 3A以上）

- **ソフトウェア**
  - Arduino IDE + micro-ROSライブラリ
  - ROS2 Humble 以降
  - Python 3.10以降
  - Wi-Fi環境（ESP32とPCが同一ネットワーク）

### 2. ROS2環境のセットアップ

```bash
# ROS2パッケージをワークスペースにコピー
mkdir -p ~/ros2_ws/src
cp -r ros2_package/servo_controller ~/ros2_ws/src/

# ビルド
cd ~/ros2_ws
colcon build --packages-select servo_controller
source install/setup.bash
```

### 3. micro-ROS Agentの起動

```bash
# micro-ROS Agentをインストール（未インストールの場合）
pip install micro-ros-agent

# UDP経由でAgentを起動（ポート8888）
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

### 4. ESP32への書き込み

1. Arduino IDEに micro-ROS for Arduino ライブラリをインストール
2. `src/esp32_servo_microros/esp32_servo_microros.ino` を開く
3. Wi-Fi設定とAgentのIPアドレスを設定

   ```cpp
   #define WIFI_SSID     "your_wifi_ssid"
   #define WIFI_PASSWORD "your_wifi_password"
   #define AGENT_IP      "192.168.1.100"  // PCのIPアドレス
   #define AGENT_PORT    8888
   ```

4. ボードを `ESP32 Dev Module` に設定してスケッチを書き込む

### 5. ハードウェアの接続

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

### 6. ROS2ノードの起動

```bash
# ターミナル1: servo_bridge_node + script_runner_node を同時起動
ros2 launch servo_controller servo_controller.launch.py

# または個別に起動
# ターミナル1: サーボ制御ブリッジ（port 8080）
ros2 run servo_controller servo_bridge_node.py

# ターミナル2: スクリプト実行ノード（port 8081）
ros2 run servo_controller script_runner_node.py
```

### 7. Webインターフェースの起動

1. `web_ui/script_controller_ros2.html` または `web_ui/block_editor/index.html` をブラウザで開く
2. ホスト名（デフォルト: `localhost`）を確認
3. 「接続テスト」ボタンをクリック
4. スクリプトを入力し「アップロード」→「実行」

## 🌐 HTTP API リファレンス

### servo_bridge_node（port 8080）

| メソッド | エンドポイント | 説明 |
| --- | --- | --- |
| GET | `/api/servo/status` | 全サーボの角度を取得 |
| GET | `/api/servos` | 全サーボの角度を取得 |
| POST | `/api/servo/{ch}` | 個別サーボを制御 |
| POST | `/api/servos` | 複数サーボを同時制御 |

### script_runner_node（port 8081）

| メソッド | エンドポイント | 説明 |
| --- | --- | --- |
| POST | `/api/script/upload` | スクリプトをアップロード |
| POST | `/api/script/execute` | スクリプトを実行 |
| POST | `/api/script/stop` | 実行を停止 |
| GET | `/api/script/status` | 実行状態を取得 |

## 📡 ROS2 トピック

| トピック | 型 | 方向 | 説明 |
| --- | --- | --- | --- |
| `/servo_command` | `std_msgs/Int32MultiArray` | PC → ESP32 | `[channel, angle]` |
| `/servos_command` | `std_msgs/Int32MultiArray` | PC → ESP32 | `[ch0, a0, ch1, a1, ...]` |
| `/servo_state` | `std_msgs/Int32MultiArray` | ESP32 → PC | `[angle0, ..., angle9]` |

### コマンドラインからトピックを直接操作

```bash
# サーボ0を90度に
ros2 topic pub --once /servo_command std_msgs/msg/Int32MultiArray \
  "{data: [0, 90]}"

# サーボ0,1,2を同時制御
ros2 topic pub --once /servos_command std_msgs/msg/Int32MultiArray \
  "{data: [0, 90, 1, 45, 2, 135]}"

# サーボ状態を確認
ros2 topic echo /servo_state
```

## 📝 スクリプト言語

`web_api` / `serial_api` 版と同じ構文を使用できます。

```text
# 個別サーボ制御
servo 0 90

# 複数サーボ同時制御
servos 0:90 1:45 2:135

# 待機（ミリ秒）
wait 1000

# 条件分岐（ブリッジノード側で評価）
if servo0 == 90
  servo 0 0
else
  servo 0 90
endif
```

## 🔧 トラブルシューティング

### ESP32がAgentに接続できない

- PCとESP32が同じWi-Fiネットワークにあるか確認
- `AGENT_IP` がPCの正しいIPアドレスか確認（`ip addr` または `ifconfig` で確認）
- ファイアウォールがUDPポート8888を許可しているか確認

### ROS2ノードが起動しない

- `source ~/ros2_ws/install/setup.bash` を実行済みか確認
- `colcon build` が成功しているか確認

### WebUIから接続できない

- `servo_bridge_node` が起動しているか確認（port 8080）
- ブラウザのCORSエラーはノード側で `Access-Control-Allow-Origin: *` を設定済み

## � 詳細ドキュメント

完全なマニュアルは [`ROS2_Manual.md`](ROS2_Manual.md) を参照してください。

- アーキテクチャ詳細・構成図
- 環境構築手順（ROS2 / micro-ROS Agent / Arduino IDE）
- ESP32スケッチの設定・書き込み手順
- ROS2パッケージのビルド・起動手順
- スクリプト言語リファレンス（条件分岐含む）
- HTTP APIリファレンス
- ROS2コマンドラインでの操作例
- 他のROS2ノードからの利用例（Python）
- トラブルシューティング

## �� ライセンス

[MIT License](../LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56
