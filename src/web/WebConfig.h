/*
 * WebConfig.h
 * ================================================================
 * ESP32-C3热点网页配置接口。
 *
 * 本类只负责：
 *   1. 手机浏览器页面
 *   2. HTTP参数
 *   3. Preferences配置
 *   4. 调用BmsBle连接/扫描
 *
 * 本类不负责：
 *   - JK/ANT/TT等原始协议解析
 *   - ST7789绘图
 *   - 修改BmsData的协议原始字段
 *
 * 屏幕配置专用接口：
 *   GET  /display
 *   GET  /api/display
 *   POST /api/display/save
 *
 * DisplayConfig是WebConfig和Display之间的配置桥梁。
 * ================================================================
 */
#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <Arduino.h>
#include <WebServer.h>
#include "../ble/BmsBle.h"
#include "../display/DisplayConfig.h"

class WebConfig {
public:
  WebConfig();

  // 在热点模式启动HTTP服务器并注册路由。
  void begin(BmsBle* ble);

  // 主循环调用，处理手机浏览器请求。
  void loop();

  // 判断网页服务当前是否已经启动。
  bool active() const { return active_; }

private:
  WebServer server_;
  BmsBle* ble_;
  bool active_;

  // HTML页面。
  void handleRoot();
  void handleProtection();
  void handleDisplay();

  // JSON/API接口。
  void handleDisplayStatus();
  void handleStatus();
  void handleRestart();
  void handleProtectionSave();
  void handleProtectionClear();
  void handleScan();
  void handleConnect();
  void handleSave();
  void handleDisplaySave();

  // 未匹配到路由时返回404。
  void handleNotFound();

  // JSON字符串转义，防止设备名称/MAC等字符串破坏JSON。
  String jsonEscape(const String& s);

  // 生成主状态页面使用的BMS JSON。
  String makeStatusJson();
};

#endif
