#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include "esp_system.h"

// Arduino IDE 标准 Sketch 主文件。
// 功能模块采用 .h/.cpp 形式，主入口位于 90_Main.ino。
// 本文件只放 Arduino/第三方库的公共 include，避免重复定义。