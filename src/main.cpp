#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include "esp_system.h"
#include "tft_setup.h"
#include "BmsData.h"
#include "ble/BmsBle.h"
#include "display/Display.h"
#include "web/WebConfig.h"

static BmsBle bmsBle;
static Display display;
static WebConfig webConfig;
static const char* AP_SSID="JK-BMS-SETUP";
static const char* AP_PASSWORD="12345678";

RTC_DATA_ATTR static uint8_t rtcFailedScanAttempts=0;

static void startHotspot(){
  // 三次 BLE 扫描后进入配网时，先关闭 WiFi 驱动的旧状态，
  // 再只启动 SoftAP，避免 BLE + WiFi 切换时保留无用的 STA 资源。
  if(g_bmsData.hotspot){
    Serial.printf("HOTSPOT: already active, skip duplicate begin, heap=%u\\n",ESP.getFreeHeap());
    return;
  }

  Serial.printf("HOTSPOT: before WiFi AP heap=%u\\n",ESP.getFreeHeap());
  WiFi.mode(WIFI_OFF);
  delay(80);
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
  g_bmsData.scanAttempt=0;
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.hotspot=false;
  g_bmsData.bootState=BOOT_START;
  g_bmsData.statusMessage="连接电池";

  // 保持 V2 最后确认 OK 的开机连接流程：
  // 连接电池 -> 扫描 1/3 -> 2/3 -> 3/3 -> 找到后连接并验证 JK 数据。
  // 只有连续 3 次扫描/连接均失败才进入热点配网。
  display.begin();
  display.update(g_bmsData);
  bmsBle.begin();

  bool connectedOk=false;

  for(uint8_t attempt=1; attempt<=3; attempt++){
    g_bmsData.scanAttempt=attempt;
    g_bmsData.bootState=BOOT_SCANNING;
    g_bmsData.statusMessage="扫描蓝牙电池 "+String(attempt)+"/3";
    display.update(g_bmsData);

    Serial.printf("BOOT: V2 scan attempt %u/3\\n",attempt);

    if(bmsBle.scanAndConnect(3,attempt)){
      // GATT 已连接后，必须收到有效 JK 数据才算真正成功。
      uint32_t verifyStart=millis();
      while(bmsBle.connected() && !g_bmsData.valid &&
            millis()-verifyStart<4000UL){
        bmsBle.loop();
        display.update(g_bmsData);
        delay(20);
      }

      if(bmsBle.connected() && g_bmsData.valid){
        connectedOk=true;
        break;
      }

      bmsBle.releaseConnectionForHotspot();
    }

    g_bmsData.online=false;
    g_bmsData.valid=false;

    if(attempt<3){
      g_bmsData.bootState=BOOT_SCANNING;
      g_bmsData.statusMessage="第 "+String(attempt)+"/3 次未连接，继续扫描";
      display.update(g_bmsData);
      delay(300);
    }
  }

  if(connectedOk){
    g_bmsData.bootState=BOOT_CONNECTED;
    g_bmsData.online=true;
    g_bmsData.statusMessage="已连接JK电池";
    display.update(g_bmsData);
    Serial.println("BOOT: V2 connection screen flow completed, JK data valid.");
    return;
  }

  // V2 原流程：三次扫描/连接失败后进入热点模式。
  bmsBle.releaseConnectionForHotspot();
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="蓝牙连接失败，进入配网";
  display.update(g_bmsData);
  delay(100);

  Serial.printf("BOOT: 3 scan attempts failed, before hotspot heap=%u\\n",ESP.getFreeHeap());
  startHotspot();
  Serial.printf("BOOT: hotspot ready heap=%u\\n",ESP.getFreeHeap());
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
