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
#include <SimpleWiFiManager.h>
#include <WebServer.h>
#include <ESP32FwUploader.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/int32_multi_array.h>
#include <std_msgs/msg/multi_array_dimension.h>

// ===== Wi-Fi / micro-ROS Agent設定 =====
// 初回起動・リセット時: AP "ESP32-ServoAP" が起動 → 192.168.4.1 でSSID/パスワード+Agent IPを設定
// IO0ピンを5秒長押しで設定リセット
#define AGENT_PORT       8888
#define AGENT_IP_DEFAULT "192.168.1.100"
#define NVS_NAMESPACE    "microros"
#define NVS_KEY_AGENT    "agent_ip"
#define RESET_PIN        0      // IO0 (BOOT button)
#define RESET_HOLD_MS    5000  // 長押し判定時間(ms)

Preferences prefs;
char agentIp[40] = AGENT_IP_DEFAULT;
static char wifiSsid[40];
static char wifiPassword[64];

// ===== OTA設定 =====
static const char*    OTA_USERNAME = "admin";
static const char*    OTA_PASSWORD = "password123";
static const uint16_t OTA_PORT     = 8080;
WebServer otaServer(OTA_PORT);

// ===== OLED設定 (SSD1306 128x32) =====
#define OLED_SDA      21
#define OLED_SCL      22
#define OLED_ADDR     0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledAvailable = false;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ===== 前方宣言 =====
void oledPrint(const String &line1, const String &line2 = "", const String &line3 = "");

// ===== IO0長押しリセット確認 =====
void checkResetButton() {
  if (digitalRead(RESET_PIN) == LOW) {
    unsigned long pressStart = millis();
    oledPrint("Hold to reset", "Release to cancel", "5sec -> RESET");
    Serial.println("Reset button held, waiting...");
    while (digitalRead(RESET_PIN) == LOW) {
      unsigned long held = millis() - pressStart;
      if (held >= RESET_HOLD_MS) {
        Serial.println("Resetting WiFi settings...");
        oledPrint("Resetting...", "WiFi + Agent IP", "Restarting...");
        SimpleWiFiManager wm;
        wm.resetSettings();
        prefs.begin(NVS_NAMESPACE, false);
        prefs.clear();
        prefs.end();
        delay(1000);
        ESP.restart();
      }
      delay(100);
    }
    oledPrint("ESP32 micro-ROS", "Initializing...");
  }
}

// ===== OLED表示ヘルパー =====
void oledPrint(const String &line1, const String &line2, const String &line3) {
  if (!oledAvailable) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);  oled.println(line1);
  oled.setCursor(0, 11); oled.println(line2);
  oled.setCursor(0, 22); oled.println(line3);
  oled.display();
}

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
  oledPrint("micro-ROS Error", "Check Agent", "LED blinking...");
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
  pinMode(RESET_PIN, INPUT_PULLUP);

  // ===== OLED初期化 =====
  Wire.begin(OLED_SDA, OLED_SCL);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledAvailable = true;
    oledPrint("ESP32 micro-ROS", "Initializing...");
  } else {
    Serial.println("OLED not found");
  }

  // ===== IO0長押しリセット確認 =====
  checkResetButton();

  // ===== サーボ初期化 =====
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
  oledPrint("ESP32 micro-ROS", "Servos OK", "WiFi connecting...");

  // ===== NVSからAgent IPを読み込み =====
  prefs.begin(NVS_NAMESPACE, true);
  String savedIp = prefs.getString(NVS_KEY_AGENT, AGENT_IP_DEFAULT);
  prefs.end();
  savedIp.toCharArray(agentIp, sizeof(agentIp));
  Serial.printf("Saved Agent IP: %s\n", agentIp);

  // ===== SimpleWiFiManager =====
  WiFiManagerParameter agentIpParam("agent_ip", "micro-ROS Agent IP", agentIp, 39);

  SimpleWiFiManager wm;
  wm.addParameter(&agentIpParam);
  wm.setConfigPortalTimeout(120);
  wm.setSaveConfigCallback([]() {
    Serial.println("[WiFiManager] Config saved");
  });
  wm.setAPCallback([](SimpleWiFiManager *wm) {
    Serial.println("Config portal started");
    oledPrint("WiFi Setup AP", "ESP32-ServoAP", "192.168.4.1");
    digitalWrite(LED_BUILTIN, HIGH);
  });

  if (!wm.autoConnect("ESP32-ServoAP")) {
    Serial.println("WiFi connection failed. Restarting...");
    oledPrint("WiFi Failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  // カスタムパラメータからAgent IPを取得・NVSに保存
  strncpy(agentIp, agentIpParam.getValue(), sizeof(agentIp) - 1);
  agentIp[sizeof(agentIp) - 1] = '\0';
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY_AGENT, agentIp);
  prefs.end();
  Serial.printf("Agent IP: %s\n", agentIp);

  Serial.println("WiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, LOW);
  oledPrint("ESP32 micro-ROS", WiFi.localIP().toString(), "Agent:" + String(agentIp));

  // ===== ESP32FwUploader (OTA) =====
  ESP32FwUploader.setDebug(true);
  ESP32FwUploader.setDarkMode(false);
  ESP32FwUploader.setAuth(OTA_USERNAME, OTA_PASSWORD);
  ESP32FwUploader.setAutoReboot(true);

  ESP32FwUploader.onStart([]() {
    Serial.println("[OTA] Started");
    oledPrint("OTA Update", "Starting...");
    digitalWrite(LED_BUILTIN, HIGH);
  });
  ESP32FwUploader.onProgress([](size_t current, size_t total) {
    if (total > 0) {
      int pct = (int)((float)current / (float)total * 100.0f);
      Serial.printf("[OTA] Progress: %d%% (%u/%u bytes)\n", pct, (unsigned)current, (unsigned)total);
      oledPrint("OTA Update", String(pct) + "%");
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  });
  ESP32FwUploader.onEnd([](bool success) {
    Serial.printf("[OTA] End: %s\n", success ? "SUCCESS" : "FAILED");
    if (success) {
      oledPrint("OTA Update", "Complete!", "Rebooting...");
    } else {
      oledPrint("OTA Failed", ESP32FwUploader.getLastErrorMessage());
    }
    digitalWrite(LED_BUILTIN, LOW);
  });
  ESP32FwUploader.onError([](ESP32Fw_Error error, const String& message) {
    Serial.printf("[OTA] Error %d: %s\n", error, message.c_str());
    oledPrint("OTA Error", message);
  });

  ESP32FwUploader.begin(&otaServer);
  otaServer.begin();
  Serial.printf("OTA URL: http://%s:%u/update\n", WiFi.localIP().toString().c_str(), OTA_PORT);
  Serial.printf("OTA Username: %s\n", OTA_USERNAME);
  Serial.printf("OTA Password: %s\n", OTA_PASSWORD);

  // ===== micro-ROS トランスポート設定（接続済みWi-Fiを使用） =====
  oledPrint("micro-ROS", "Connecting agent...", String(agentIp));
  
  // WiFi credentials must be stored in persistent buffers (global)
  WiFi.SSID().toCharArray(wifiSsid, sizeof(wifiSsid));
  WiFi.psk().toCharArray(wifiPassword, sizeof(wifiPassword));
  
  set_microros_wifi_transports(
    wifiSsid,
    wifiPassword,
    agentIp,
    AGENT_PORT);
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
  Serial.printf("Agent: %s:%d\n", agentIp, AGENT_PORT);
  oledPrint("micro-ROS Ready", WiFi.localIP().toString(), "Agent:" + String(agentIp));

  // 初期状態をパブリッシュ
  publishState();
}

void loop() {
  otaServer.handleClient();
  ESP32FwUploader.loop();

  RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));
  delay(10);
}
