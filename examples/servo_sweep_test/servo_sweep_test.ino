// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tomorrow56
// https://github.com/tomorrow56/esp32-servo-controller

// Servo Sweep Test
// 10個のサーボを順番に 0度 → 180度 → 0度 と動かすテストスケッチ
// 対応ボード: ESP32
// 使用ライブラリ: ESP32Servo

#include <ESP32Servo.h>

// ===== サーボ設定 =====
static const int NUM_SERVOS                  = 10;
static const int SERVO_PINS[NUM_SERVOS]      = {23, 19, 18, 5, 17, 16, 4, 27, 14, 12};
static const int SERVO_MIN_US                = 500;   // パルス幅最小値 (us)
static const int SERVO_MAX_US                = 2500;  // パルス幅最大値 (us)

// ===== スイープ設定 =====
static const int ANGLE_MIN                   = 0;     // 最小角度 (度)
static const int ANGLE_MAX                   = 180;   // 最大角度 (度)
static const int STEP_DELAY_MS               = 15;    // 1ステップあたりの待機時間 (ms)
static const int SERVO_INTERVAL_MS           = 300;   // 各サーボ動作間のインターバル (ms)

Servo servos[NUM_SERVOS];

// 1つのサーボを angle_from → angle_to へスイープする
void sweepServo(int idx, int angleFrom, int angleTo) {
  int step = (angleTo > angleFrom) ? 1 : -1;
  for (int angle = angleFrom; angle != angleTo + step; angle += step) {
    servos[idx].write(angle);
    delay(STEP_DELAY_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Servo Sweep Test - Start");

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(SERVO_PINS[i], SERVO_MIN_US, SERVO_MAX_US);
    servos[i].write(ANGLE_MIN);
    delay(20);
  }

  Serial.println("All servos initialized at 0 degrees.");
  delay(1000);
}

void loop() {
  // 順番に 0 → 180 → 0 へスイープ
  for (int i = 0; i < NUM_SERVOS; i++) {
    Serial.printf("CH%d: %d -> %d deg\n", i, ANGLE_MIN, ANGLE_MAX);
    sweepServo(i, ANGLE_MIN, ANGLE_MAX);
    delay(SERVO_INTERVAL_MS);

    Serial.printf("CH%d: %d -> %d deg\n", i, ANGLE_MAX, ANGLE_MIN);
    sweepServo(i, ANGLE_MAX, ANGLE_MIN);
    delay(SERVO_INTERVAL_MS);
  }

  Serial.println("--- One cycle complete ---");
}
