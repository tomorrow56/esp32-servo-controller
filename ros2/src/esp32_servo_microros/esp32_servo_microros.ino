// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tomorrow56
// https://github.com/tomorrow56/esp32-servo-controller

// micro-ROS on ESP32 - Servo Controller Node
// Subscribes to:
//   /servo_command  (servo_controller_msgs/msg/ServoCommand)
//   /servos_command (servo_controller_msgs/msg/ServosCommand)
// Publishes to:
//   /servo_state    (servo_controller_msgs/msg/ServoState)

#include <micro_ros_arduino.h>
#include <ESP32Servo.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/int32_multi_array.h>
#include <std_msgs/msg/multi_array_dimension.h>

// ===== Wi-Fi設定（micro-ROS over Wi-Fi UDP） =====
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define AGENT_IP      "192.168.1.100"  // micro-ROS Agentが動作するPCのIPアドレス
#define AGENT_PORT    8888

// ===== サーボ設定 =====
const int NUM_SERVOS = 10;
const int SERVO_PINS[NUM_SERVOS] = {23, 19, 18, 5, 17, 16, 4, 27, 14, 12};
Servo servos[NUM_SERVOS];
int servoAngles[NUM_SERVOS];

// ===== micro-ROS =====
rcl_node_t           node;
rcl_subscription_t   servoSub;
rcl_subscription_t   servosSub;
rcl_publisher_t      statePub;
rclc_executor_t      executor;
rclc_support_t       support;
rcl_allocator_t      allocator;

// メッセージ型: Int32MultiArray を流用
// servo_command:  [channel, angle]
// servos_command: [ch0, angle0, ch1, angle1, ...]
// servo_state:    [angle0, angle1, ..., angle9]
std_msgs__msg__Int32MultiArray servoMsg;
std_msgs__msg__Int32MultiArray servosMsg;
std_msgs__msg__Int32MultiArray stateMsg;

int32_t servoData[2];
int32_t servosData[NUM_SERVOS * 2];
int32_t stateData[NUM_SERVOS];

#define RCCHECK(fn)  { rcl_ret_t temp_rc = fn; if (temp_rc != RCL_RET_OK) { errorLoop(); } }
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if (temp_rc != RCL_RET_OK) {} }

// ===== エラー時LED点滅 =====
void errorLoop() {
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
  }
}

// ===== サーボ状態をパブリッシュ =====
void publishState() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    stateData[i] = servoAngles[i];
  }
  stateMsg.data.data  = stateData;
  stateMsg.data.size  = NUM_SERVOS;
  stateMsg.data.capacity = NUM_SERVOS;
  RCSOFTCHECK(rcl_publish(&statePub, &stateMsg, NULL));
}

// ===== /servo_command コールバック =====
// データ形式: [channel, angle]
void servoCallback(const void* msgin) {
  const std_msgs__msg__Int32MultiArray* msg =
    (const std_msgs__msg__Int32MultiArray*)msgin;
  if (msg->data.size < 2) return;

  int ch    = (int)msg->data.data[0];
  int angle = (int)msg->data.data[1];

  if (ch < 0 || ch >= NUM_SERVOS) return;
  angle = constrain(angle, 0, 180);

  servos[ch].write(angle);
  servoAngles[ch] = angle;

  Serial.printf("[servo_command] CH%d -> %d deg\n", ch, angle);
  publishState();
}

// ===== /servos_command コールバック =====
// データ形式: [ch0, angle0, ch1, angle1, ...]
void servosCallback(const void* msgin) {
  const std_msgs__msg__Int32MultiArray* msg =
    (const std_msgs__msg__Int32MultiArray*)msgin;
  size_t count = msg->data.size / 2;

  for (size_t i = 0; i < count; i++) {
    int ch    = (int)msg->data.data[i * 2];
    int angle = (int)msg->data.data[i * 2 + 1];
    if (ch < 0 || ch >= NUM_SERVOS) continue;
    angle = constrain(angle, 0, 180);
    servos[ch].write(angle);
    servoAngles[ch] = angle;
    Serial.printf("[servos_command] CH%d -> %d deg\n", ch, angle);
  }
  publishState();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // サーボ初期化
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(SERVO_PINS[i], 500, 2500);
    servoAngles[i] = 90;
    servos[i].write(servoAngles[i]);
    delay(20);
  }
  Serial.println("Servos initialized.");

  // micro-ROS トランスポート設定（Wi-Fi UDP）
  set_microros_wifi_transports(
    WIFI_SSID, WIFI_PASSWORD, AGENT_IP, AGENT_PORT);
  delay(2000);

  // micro-ROS 初期化
  allocator = rcl_get_default_allocator();
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "esp32_servo_node", "", &support));

  // サブスクライバー初期化
  RCCHECK(rclc_subscription_init_default(
    &servoSub, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32MultiArray),
    "/servo_command"));

  RCCHECK(rclc_subscription_init_default(
    &servosSub, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32MultiArray),
    "/servos_command"));

  // パブリッシャー初期化
  RCCHECK(rclc_publisher_init_default(
    &statePub, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32MultiArray),
    "/servo_state"));

  // メッセージバッファ設定
  servoMsg.data.data     = servoData;
  servoMsg.data.size     = 0;
  servoMsg.data.capacity = 2;

  servosMsg.data.data     = servosData;
  servosMsg.data.size     = 0;
  servosMsg.data.capacity = NUM_SERVOS * 2;

  // エグゼキューター初期化（サブスクライバー2つ）
  RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
  RCCHECK(rclc_executor_add_subscription(
    &executor, &servoSub, &servoMsg, &servoCallback, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_subscription(
    &executor, &servosSub, &servosMsg, &servosCallback, ON_NEW_DATA));

  Serial.println("micro-ROS node started: esp32_servo_node");
  Serial.printf("Agent: %s:%d\n", AGENT_IP, AGENT_PORT);

  // 初期状態をパブリッシュ
  publishState();
}

void loop() {
  RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));
  delay(10);
}
