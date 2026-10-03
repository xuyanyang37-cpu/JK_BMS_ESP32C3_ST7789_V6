/*
 * ================================================================
 * JK_BMS_ESP32C3_ST7789_V6
 * 主程序业务流程说明
 *
 * 【开机总流程】
 *  01. setup()
 *  02. 初始化串口、背光、BMS状态
 *  03. Display.begin()     -> 初始化 ST7789
 *  04. BmsBle.begin()      -> 初始化 BLE、读取保存配置
 *  05. 连续最多 3 次执行：
 *      扫描 -> 找候选设备 -> 连接 -> 找 FFE0 -> 找可写/通知特征
 *      -> 订阅通知 -> 主动请求数据 -> 等待有效 BMS 数据
 *  06. BLE 已连接 + g_bmsData.valid == true -> 正常进入 loop()
 *  07. 3 次都失败 -> 释放 BLE -> 启动 SoftAP -> WebConfig 网页配网
 *
 * 【运行期 loop】
 *  BLE处理 -> 网页处理 -> 每500ms刷新一次显示
 *
 * 【重要分支】
 *  A. 找不到设备             -> 下一次扫描
 *  B. 找到但连接失败         -> 下一次扫描
 *  C. 连接成功但没有有效数据 -> 释放连接 -> 下一次扫描
 *  D. 3次全部失败            -> HOTSPOT 配网
 *  E. 正常运行中 BLE断开      -> BmsBle.loop() 每5秒恢复一次
 *  F. 任意运行期恢复连续3次失败 -> HOTSPOT 配网
 *
 * 这里尽量只写“为什么”，具体协议解析请看 protocol/ 目录。
 * ================================================================
 */

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

/*
 * 业务分支：启动热点配网。
 *
 * 为什么单独做成函数？
 * 因为“3次 BLE 失败 -> 热点”是一个明确的业务状态，
 * 后续网页配置、内存优化都集中在这里比较容易看懂。
 */
static void startHotspot(){
  // 已经启动过就直接返回，避免重复初始化 WebServer/WiFi。
  if(g_bmsData.hotspot){
    Serial.printf("HOTSPOT: already active, skip duplicate begin, heap=%u\n",ESP.getFreeHeap());
    return;
  }

  Serial.printf("HOTSPOT: before WiFi AP heap=%u\n",ESP.getFreeHeap());

  // 先彻底关闭旧 WiFi 状态，再只开启 AP。
  // 目的：BLE扫描/连接失败后释放 WiFi 旧资源，降低 ESP32-C3 内存压力。
  WiFi.mode(WIFI_OFF);
  delay(80);

  // AP 模式：手机连接 JK-BMS-SETUP 后访问 192.168.4.1。
  WiFi.mode(WIFI_AP);

  // 最后一个参数限制 AP 最大客户端数为 1。
  // 本项目只需要一台手机配置，因此可以少占一些资源。
  WiFi.softAP(AP_SSID,AP_PASSWORD,1,false,1);

  IPAddress ip=WiFi.softAPIP();

  g_bmsData.hotspot=true;
  g_bmsData.hotspotIp=ip.toString();
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="等待网页设置";

  // WebConfig 只负责网页，不负责 BLE 底层连接。
  webConfig.begin(&bmsBle);

  Serial.printf("HOTSPOT: %s %s heap=%u\n",
                AP_SSID,ip.toString().c_str(),ESP.getFreeHeap());
}

void setup(){
  // ================================================================
  // 1. MCU启动
  // ================================================================
  Serial.begin(115200);
  delay(50);

  // 打印复位原因。
  // 如果以后出现“3次失败后自动重启”，第一时间看这里。
  esp_reset_reason_t resetReason=esp_reset_reason();
  Serial.println();
  Serial.printf("ESP32 reset reason: %d\n",(int)resetReason);

  // ================================================================
  // 2. 初始化屏幕背光
  // ================================================================
  pinMode(TFT_BL,OUTPUT);
  digitalWrite(TFT_BL,LOW);
  delay(20);

  // ================================================================
  // 3. 初始化本次启动的 BMS 状态
  // ================================================================
  g_bmsData.scanMax=3;
  g_bmsData.scanAttempt=0;
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.hotspot=false;
  g_bmsData.bootState=BOOT_START;
  g_bmsData.statusMessage="连接电池";

  // ================================================================
  // 4. 初始化显示和 BLE
  // ================================================================
  // Display：只负责“画什么”。
  display.begin();
  display.update(g_bmsData);

  // BmsBle：负责扫描、连接、通知接收、发送查询。
  bmsBle.begin();

  bool connectedOk=false;

  // ================================================================
  // 5. V2确认过的三次启动连接流程
  // ================================================================
  for(uint8_t attempt=1; attempt<=3; attempt++){
    g_bmsData.scanAttempt=attempt;
    g_bmsData.bootState=BOOT_SCANNING;
    g_bmsData.statusMessage="扫描蓝牙电池 "+String(attempt)+"/3";
    display.update(g_bmsData);

    Serial.printf("BOOT: V2 scan attempt %u/3\n",attempt);

    /*
     * scanAndConnect()内部继续细分：
     *   扫描
     *     -> 有保存MAC？优先连接保存MAC
     *     -> 没有/失败？从扫描结果选择RSSI最强候选
     *   连接
     *     -> 找FFE0
     *     -> 找可写特征
     *     -> 找通知特征
     *     -> 订阅通知
     *     -> 发送JK/ANT/TT探测
     */
    if(bmsBle.scanAndConnect(3,attempt)){
      // 注意：
      // “BLE GATT连接成功”不等于“BMS连接成功”。
      // 必须真正收到能解析的数据，g_bmsData.valid才成立。
      uint32_t verifyStart=millis();

      while(bmsBle.connected() && !g_bmsData.valid &&
            millis()-verifyStart<4000UL){
        bmsBle.loop();
        display.update(g_bmsData);
        delay(20);
      }

      // 分支：连接成功 + 收到有效BMS数据 -> 启动成功。
      if(bmsBle.connected() && g_bmsData.valid){
        connectedOk=true;
        break;
      }

      // 分支：连接了，但4秒内没有有效数据。
      // 释放当前连接，下一轮重新扫描。
      bmsBle.releaseConnectionForHotspot();
    }

    // 本轮失败：清除在线/有效状态。
    g_bmsData.online=false;
    g_bmsData.valid=false;

    // 前两次失败继续，第3次失败则跳出后进入热点。
    if(attempt<3){
      g_bmsData.bootState=BOOT_SCANNING;
      g_bmsData.statusMessage="第 "+String(attempt)+"/3 次未连接，继续扫描";
      display.update(g_bmsData);
      delay(300);
    }
  }

  // ================================================================
  // 6A. 正常连接成功分支
  // ================================================================
  if(connectedOk){
    g_bmsData.bootState=BOOT_CONNECTED;
    g_bmsData.online=true;
    String proto=bmsBle.getPreferredProtocol();
    g_bmsData.statusMessage="已连接 "+proto+" 电池";
    display.update(g_bmsData);

    // 首次启动已经完成“连接 + 有效数据”验证。
    // 从这里开始，运行期断线恢复才启用独立的3次失败状态机。
    bmsBle.setRuntimeRecoveryEnabled(true);
    Serial.printf("BOOT: connection verified, protocol=%s, runtime recovery enabled.\\n",proto.c_str());

    // setup() return 后，Arduino自动进入 loop()。
    return;
  }

  // ================================================================
  // 6B. 三次都失败 -> 热点配网分支
  // ================================================================
  bmsBle.releaseConnectionForHotspot();

  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="蓝牙连接失败，进入配网";
  display.update(g_bmsData);
  delay(100);

  Serial.printf("BOOT: 3 scan attempts failed, before hotspot heap=%u\n",
                ESP.getFreeHeap());

  startHotspot();

  Serial.printf("BOOT: hotspot ready heap=%u\n",ESP.getFreeHeap());
  display.update(g_bmsData);
}

void loop(){
  /*
   * ================================================================
   * 运行期业务循环
   * ================================================================
   *
   * 每一圈的顺序固定：
   *
   * ① BLE loop
   *    - 检查连接
   *    - 接收通知
   *    - 拼接/解析协议帧
   *    - 定时发送查询
   *    - 断线后按条件重连
   *
   * ② Web loop
   *    - 只有热点模式 active_ 时才处理HTTP请求
   *
   * ③ Display
   *    - 每500ms检查一次数据
   *    - Display内部再决定是整屏还是局部刷新
   *
   * ④ delay(5)
   *    - 给BLE/WiFi后台任务留出运行时间
   */
  bmsBle.loop();

  // BmsBle负责判断“连续3次恢复失败”，但不直接操作WebConfig。
  // 一旦进入BOOT_HOTSPOT，由main统一启动SoftAP和网页服务。
  if(g_bmsData.bootState==BOOT_HOTSPOT && !g_bmsData.hotspot){
    startHotspot();
  }

  webConfig.loop();

  static uint32_t drawMs=0;
  if(millis()-drawMs>=500){
    drawMs=millis();
    display.update(g_bmsData);
  }

  delay(5);
}
