# ESP32 サーボ制御システム ROS2版 詳細マニュアル

**著者**: tomorrow56  
**バージョン**: 1.0  
**最終更新**: 2026年2月

---

## 目次

1. [システム概要](#1-システム概要)
2. [アーキテクチャ詳細](#2-アーキテクチャ詳細)
3. [環境構築](#3-環境構築)
4. [ESP32のセットアップ](#4-esp32のセットアップ)
5. [ROS2パッケージのセットアップ](#5-ros2パッケージのセットアップ)
6. [起動手順](#6-起動手順)
7. [Webインターフェースの使い方](#7-webインターフェースの使い方)
8. [スクリプト言語リファレンス](#8-スクリプト言語リファレンス)
9. [HTTP APIリファレンス](#9-http-apiリファレンス)
10. [ROS2トピックリファレンス](#10-ros2トピックリファレンス)
11. [ROS2コマンドラインでの操作](#11-ros2コマンドラインでの操作)
12. [トラブルシューティング](#12-トラブルシューティング)

---

## 1. システム概要

本システムはESP32マイクロコントローラー上でmicro-ROSを動作させ、ROS2エコシステムと統合してサーボモーターを制御します。

### 他バージョンとの比較

| 項目 | web_api版 | serial_api版 | **ROS2版** |
| --- | --- | --- | --- |
| 通信方式 | Wi-Fi HTTP REST | USB シリアル | **Wi-Fi micro-ROS UDP** |
| スクリプト実行 | ESP32側 | ブラウザ側 | **PC側（ROS2ノード）** |
| ROS2統合 | なし | なし | **ネイティブ対応** |
| コマンドライン制御 | 不可 | 不可 | **`ros2 topic pub` で可能** |
| 他ROS2ノードとの連携 | 不可 | 不可 | **可能** |
| 必要環境 | Wi-Fi | USBケーブル | **Wi-Fi + ROS2** |

### 主な特徴

- **micro-ROS**: ESP32がROS2ノードとして直接動作。Wi-Fi UDP経由でmicro-ROS Agentと通信
- **ROS2トピック**: `/servo_command`・`/servos_command`・`/servo_state` で他のROS2ノードと連携可能
- **HTTP APIブリッジ**: `web_api`版と同じHTTP APIを提供するため、既存のWebUIをそのまま利用可能
- **スクリプト実行**: `if/else/endif` 条件分岐を含むスクリプトをPC側（`script_runner_node`）で実行

---

## 2. アーキテクチャ詳細

### 全体構成図

```text
┌─────────────────────────────────────────────────────────┐
│                     PC / ホストマシン                      │
│                                                         │
│  ┌──────────────────┐    ┌──────────────────────────┐   │
│  │  Webブラウザ       │    │  ros2 topic pub / echo   │   │
│  │  (Web UI)        │    │  (コマンドライン)           │   │
│  └────────┬─────────┘    └────────────┬─────────────┘   │
│           │ HTTP REST                 │ ROS2 Topic      │
│           ▼                           ▼                 │
│  ┌────────────────────────────────────────────────────┐ │
│  │              ROS2 ノード群                          │ │
│  │                                                    │ │
│  │  servo_bridge_node (port 8080)                     │ │
│  │    POST /api/servo/{ch}  → /servo_command          │ │
│  │    POST /api/servos      → /servos_command         │ │
│  │    GET  /api/servos      ← /servo_state            │ │
│  │                                                    │ │
│  │  script_runner_node (port 8081)                    │ │
│  │    POST /api/script/upload   → スクリプト保存       │ │
│  │    POST /api/script/execute  → スクリプト実行       │ │
│  │    POST /api/script/stop     → 実行停止             │ │
│  │    GET  /api/script/status   → 実行状態             │ │
│  └────────────────────────┬───────────────────────────┘ │
│                           │ ROS2 Topics                 │
│  ┌────────────────────────▼───────────────────────────┐ │
│  │          micro-ROS Agent (UDP port 8888)            │ │
│  └────────────────────────┬───────────────────────────┘ │
└───────────────────────────┼─────────────────────────────┘
                            │ UDP (Wi-Fi)
┌───────────────────────────▼─────────────────────────────┐
│                    ESP32 (micro-ROS)                    │
│                  esp32_servo_node                       │
│                                                         │
│  Subscribe: /servo_command  → サーボ個別制御             │
│  Subscribe: /servos_command → サーボ複数同時制御          │
│  Publish:   /servo_state    → 全サーボ角度               │
│                                                         │
│  GPIO: 23,19,18,5,17,16,4,27,14,12 → サーボ × 10個      │
└─────────────────────────────────────────────────────────┘
```

### ノード構成

| ノード名 | 動作場所 | HTTPポート | 役割 |
| --- | --- | --- | --- |
| `esp32_servo_node` | ESP32 | - | micro-ROSノード、サーボ制御 |
| `servo_bridge_node` | PC | 8080 | HTTP→ROS2トピックブリッジ |
| `script_runner_node` | PC | 8081 | スクリプト実行エンジン |

---

## 3. 環境構築

### 3.1 必要なもの

- ESP32-WROOM-32、サーボモーター（最大10個）、外部電源（5V / 3A以上）
- Ubuntu 22.04 LTS（推奨）または macOS
- ROS2 Humble Hawksbill 以降
- Arduino IDE 2.x + micro-ROS for Arduino ライブラリ

### 3.2 ROS2のインストール（Ubuntu 22.04）

```bash
sudo apt update && sudo apt install -y curl gnupg lsb-release
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) \
  signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu \
  $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update
sudo apt install -y ros-humble-desktop python3-colcon-common-extensions
source /opt/ros/humble/setup.bash
```

### 3.3 micro-ROS Agentのインストール

```bash
mkdir -p ~/microros_ws/src
cd ~/microros_ws
git clone -b humble https://github.com/micro-ROS/micro_ros_setup.git src/micro_ros_setup
source /opt/ros/humble/setup.bash
rosdep update && rosdep install --from-paths src --ignore-src -y
colcon build && source install/local_setup.bash
ros2 run micro_ros_setup create_agent_ws.sh
ros2 run micro_ros_setup build_agent.sh
source install/local_setup.bash
```

### 3.4 Arduino IDEのセットアップ

1. Arduino IDE 2.x をインストール
2. ボードマネージャに ESP32 を追加:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. ライブラリマネージャで以下をインストール:
   - `ESP32Servo` by Kevin Harrington
   - `micro_ros_arduino` by micro-ROS

---

## 4. ESP32のセットアップ

### 4.1 スケッチの設定

`src/esp32_servo_microros/esp32_servo_microros.ino` を開き設定します。

```cpp
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define AGENT_IP      "192.168.1.100"  // PCのIPアドレス
#define AGENT_PORT    8888
```

PCのIPアドレス確認:

```bash
# Linux
ip addr show | grep "inet " | grep -v 127.0.0.1
# macOS
ifconfig | grep "inet " | grep -v 127.0.0.1
```

### 4.2 書き込み手順

1. ボード: `ESP32 Dev Module`、Upload Speed: `921600`
2. 適切なCOMポートを選択してスケッチを書き込む
3. シリアルモニタ（115200baud）で確認:

```text
Servos initialized.
micro-ROS node started: esp32_servo_node
Agent: 192.168.1.100:8888
```

### 4.3 ハードウェアの接続

| サーボ | GPIO | ボード表記 |
| --- | --- | --- |
| CH0 | 23 | IO23 |
| CH1 | 19 | IO19 |
| CH2 | 18 | IO18 |
| CH3 | 5 | IO5 |
| CH4 | 17 | IO17 |
| CH5 | 16 | IO16 |
| CH6 | 4 | IO4 |
| CH7 | 27 | IO27 |
| CH8 | 14 | IO14 |
| CH9 | 12 | IO12 |

**重要**: サーボの電源は必ず外部電源から供給し、ESP32のGNDと共通接続してください。

```text
外部電源 5V  ──── サーボ 赤線（VCC）
外部電源 GND ──┬─ サーボ 黒/茶線（GND）
               └─ ESP32 GND
ESP32 GPIO   ──── サーボ オレンジ/黄線（信号）
```

---

## 5. ROS2パッケージのセットアップ

```bash
mkdir -p ~/ros2_ws/src
cp -r ros2/ros2_package/servo_controller ~/ros2_ws/src/
cd ~/ros2_ws
source /opt/ros/humble/setup.bash
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-select servo_controller
source install/setup.bash
```

---

## 6. 起動手順

起動順序を守ってください。

### ターミナル1: micro-ROS Agentの起動

```bash
source /opt/ros/humble/setup.bash
source ~/microros_ws/install/local_setup.bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

### ターミナル2: ROS2ノードの起動

```bash
source ~/ros2_ws/install/setup.bash

# launchファイルで両ノードを同時起動
ros2 launch servo_controller servo_controller.launch.py
```

### 動作確認

```bash
# トピック一覧
ros2 topic list
# 期待値: /servo_command, /servos_command, /servo_state

# ノード一覧
ros2 node list
# 期待値: /esp32_servo_node, /servo_bridge_node, /script_runner_node
```

---

## 7. Webインターフェースの使い方

`web_ui/script_controller_ros2.html` をブラウザで直接開きます。

1. **接続テスト**: ホスト（デフォルト: `localhost`）・ポート（`8080`）を確認してクリック
2. **スクリプト入力**: エディタに入力、またはファイルを開く
3. **アップロード**: `script_runner_node`（port 8081）にスクリプトを送信
4. **実行**: 進捗バーでリアルタイムに確認
5. **停止**: 実行中にいつでも中断可能

---

## 8. スクリプト言語リファレンス

`web_api`版・`serial_api`版と同じ構文です。

### 基本コマンド

```text
# 個別サーボ制御（channel: 0-9, angle: 0-180）
servo 0 90

# 複数サーボ同時制御
servos 0:90 1:45 2:135

# 待機（ミリ秒）
wait 1000

# コメント
# これはコメントです
```

### 条件分岐

`script_runner_node` がサーボ角度キャッシュ（`/servo_state` から更新）を参照して評価します。

```text
if servo0 == 90
  servo 0 0
  wait 500
else
  servo 0 90
endif
```

**演算子**: `==`、`!=`、`>`、`>=`、`<`、`<=`

### ネスト例

```text
if servo0 > 45
  if servo0 < 135
    servo 1 90
  else
    servo 1 180
  endif
endif
```

---

## 9. HTTP APIリファレンス

### servo_bridge_node（ポート 8080）

| メソッド | エンドポイント | 説明 |
| --- | --- | --- |
| GET | `/api/servo/status` | 全サーボ角度を取得 |
| GET | `/api/servos` | 全サーボ角度を取得 |
| POST | `/api/servo/{ch}` | 個別サーボ制御 `{"angle": 90}` |
| POST | `/api/servos` | 複数サーボ制御 `{"servos": [...]}` |

### script_runner_node（ポート 8081）

| メソッド | エンドポイント | 説明 |
| --- | --- | --- |
| POST | `/api/script/upload` | スクリプトをアップロード |
| POST | `/api/script/execute` | スクリプトを実行 |
| POST | `/api/script/stop` | 実行を停止 |
| GET | `/api/script/status` | 実行状態を取得 |

---

## 10. ROS2トピックリファレンス

| トピック | 型 | 方向 | データ形式 |
| --- | --- | --- | --- |
| `/servo_command` | `std_msgs/Int32MultiArray` | PC → ESP32 | `[channel, angle]` |
| `/servos_command` | `std_msgs/Int32MultiArray` | PC → ESP32 | `[ch0, a0, ch1, a1, ...]` |
| `/servo_state` | `std_msgs/Int32MultiArray` | ESP32 → PC | `[a0, a1, ..., a9]`（10要素） |

---

## 11. ROS2コマンドラインでの操作

```bash
# サーボ0を90度に
ros2 topic pub --once /servo_command std_msgs/msg/Int32MultiArray \
  "{data: [0, 90]}"

# サーボ0,1,2を同時制御
ros2 topic pub --once /servos_command std_msgs/msg/Int32MultiArray \
  "{data: [0, 90, 1, 45, 2, 135]}"

# 全サーボを中立位置に
ros2 topic pub --once /servos_command std_msgs/msg/Int32MultiArray \
  "{data: [0,90, 1,90, 2,90, 3,90, 4,90, 5,90, 6,90, 7,90, 8,90, 9,90]}"

# サーボ状態を確認
ros2 topic echo /servo_state --once

# 1Hzで繰り返し送信
ros2 topic pub --rate 1 /servo_command std_msgs/msg/Int32MultiArray \
  "{data: [0, 90]}"

# ノードグラフの表示
ros2 run rqt_graph rqt_graph
```

### 他のROS2ノードからの利用例（Python）

```python
import rclpy
from rclpy.node import Node
from std_msgs.msg import Int32MultiArray

class MyRobotNode(Node):
    def __init__(self):
        super().__init__('my_robot_node')
        self.pub = self.create_publisher(
            Int32MultiArray, '/servo_command', 10)

    def move_servo(self, channel: int, angle: int):
        msg = Int32MultiArray()
        msg.data = [channel, angle]
        self.pub.publish(msg)

def main():
    rclpy.init()
    node = MyRobotNode()
    node.move_servo(0, 90)
    rclpy.spin_once(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
```

---

## 12. トラブルシューティング

### ESP32がAgentに接続できない

- PCとESP32が同じWi-Fiネットワークにあるか確認
- `AGENT_IP` がPCの正しいIPアドレスか確認
- ファイアウォールがUDPポート8888を許可しているか確認

```bash
# Linuxでポートを開放
sudo ufw allow 8888/udp
```

### ROS2ノードが起動しない

```bash
# 環境変数が設定されているか確認
echo $ROS_DISTRO  # humble と表示されるはず

# 再度ソース
source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash
```

### WebUIから接続できない

- `servo_bridge_node` が起動しているか確認（port 8080）
- ブラウザのコンソールでCORSエラーが出ていないか確認
- ホスト名・ポート番号が正しいか確認

### サーボが動かない

```bash
# トピックが届いているか確認
ros2 topic echo /servo_command

# ESP32のシリアルモニタでログを確認
# [servo_command] CH0 -> 90 deg  と表示されるはず
```

### `is_executing` フラグが詰まった場合

```bash
# script_runner_node を再起動
ros2 lifecycle set /script_runner_node shutdown
ros2 run servo_controller script_runner_node.py
```

---

## 📄 ライセンス

[MIT License](../LICENSE) © 2026 tomorrow56

## 🤖 著者

tomorrow56
