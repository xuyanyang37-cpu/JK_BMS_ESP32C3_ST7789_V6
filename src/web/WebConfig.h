#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H
#include <Arduino.h>
#include <WebServer.h>
#include "../ble/BmsBle.h"
class WebConfig {
public:
  WebConfig();
  void begin(BmsBle* ble);
  void loop();
  bool active() const { return active_; }
private:
  WebServer server_;
  BmsBle* ble_;
  bool active_;
  void handleRoot();
  void handleStatus();
  void handleScan();
  void handleConnect();
  void handleSave();
  void handleNotFound();
  String jsonEscape(const String& s);
  String makeStatusJson();
  String makePage();
};
#endif
