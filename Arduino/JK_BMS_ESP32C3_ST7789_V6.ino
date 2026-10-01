#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "tft_setup.h"
#include <TFT_eSPI.h>
#include <WebServer.h>
#include "esp_system.h"

#include "BmsData.h"
#include "protocol/BmsProtocol.h"
#include "protocol/BmsProtocolManager.h"
#include "ble/BmsBle.h"
#include "display/Display.h"
#include "web/WebConfig.h"

// Arduino IDE 标准 Sketch：仅保留程序入口。
// 具体功能全部放在 .h/.cpp 模块中，避免多个 .ino 按文件名拼接造成声明顺序问题。

static BmsBle bmsBle;
static Display display;
static WebConfig webConfig;
static const char* AP_SSID="JK-BMS-SETUP";
static const char* AP_PASSWORD="12345678";

RTC_DATA_ATTR static uint8_t rtcFailedScanAttempts=0;

static void startHotspot(){
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID,AP_PASSWORD);
  IPAddress ip=WiFi.softAPIP();
  g_bmsData.hotspot=true;
  g_bmsData.hotspotIp=ip.toString();
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="等待网页设置";
  webConfig.begin(&bmsBle);
  Serial.printf("HOTSPOT: %s %s heap=%u\\n",AP_SSID,ip.toString().c_str(),ESP.getFreeHeap());
}

void setup(){
  Serial.begin(115200);
  delay(50);

  esp_reset_reason_t resetReason=esp_reset_reason();
  Serial.println();
  Serial.printf("ESP32 reset reason: %d\\n",(int)resetReason);

  pinMode(TFT_BL,OUTPUT);
  digitalWrite(TFT_BL,LOW);
  delay(20);

  g_bmsData.scanMax=3;
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.hotspot=false;

  display.begin();
  bmsBle.begin();

  const String savedMac=bmsBle.getConfiguredAddress();

  if(savedMac.length()==0){
    g_bmsData.scanAttempt=0;
    g_bmsData.bootState=BOOT_HOTSPOT;
    g_bmsData.statusMessage="未保存蓝牙，进入配网";
    display.update(g_bmsData);
    delay(100);
    startHotspot();
    display.update(g_bmsData);
    return;
  }

  g_bmsData.scanAttempt=1;
  g_bmsData.bootState=BOOT_CONNECTING;
  g_bmsData.statusMessage="连接已保存蓝牙";
  g_bmsData.mac=savedMac;
  display.update(g_bmsData);
  delay(100);

  Serial.printf("BOOT: saved JK MAC = %s type=%u\\n",
                savedMac.c_str(),bmsBle.getConfiguredAddressType());

  bool connectedOk=bmsBle.connectByAddress(
      savedMac,bmsBle.getConfiguredAddressType());

  if(connectedOk && bmsBle.connected()){
    uint32_t verifyStart=millis();
    while(bmsBle.connected() && !g_bmsData.valid &&
          millis()-verifyStart<4000UL){
      bmsBle.loop();
      delay(20);
    }
    connectedOk=bmsBle.connected() && g_bmsData.valid;
  }

  if(connectedOk){
    g_bmsData.bootState=BOOT_CONNECTED;
    g_bmsData.online=true;
    g_bmsData.statusMessage="已连接保存的JK电池";
    display.update(g_bmsData);
    Serial.println("BOOT: saved Bluetooth connected and JK data valid.");
    return;
  }

  bmsBle.releaseConnectionForHotspot();
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="蓝牙连接失败，进入配网";
  display.update(g_bmsData);
  delay(100);

  Serial.println("BOOT: saved Bluetooth connection failed, entering hotspot.");
  startHotspot();
  display.update(g_bmsData);
}

void loop(){
  bmsBle.loop();
  webConfig.loop();

  static uint32_t drawMs=0;
  if(millis()-drawMs>=500){
    drawMs=millis();
    display.update(g_bmsData);
  }
  delay(5);
}
