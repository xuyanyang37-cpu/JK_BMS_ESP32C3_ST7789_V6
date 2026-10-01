#ifndef BMS_BLE_H
#define BMS_BLE_H
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../BmsData.h"
#include "../protocol/BmsProtocolManager.h"

// 扫描结果上限统一由 BmsData.h 定义，避免不同模块出现容量不一致。

struct BmsScanItem {
  String address;
  uint8_t addressType=BLE_ADDR_PUBLIC;
  String name;
  int rssi=0;
};

class BmsBle {
public:
  BmsBle();
  bool begin();
  bool scanAndConnect(uint32_t seconds=5, uint8_t attemptOverride=0);
  bool connectByAddress(const String& address, uint8_t addressType=BLE_ADDR_PUBLIC);
  uint8_t scanDevices(uint32_t seconds=5);
  bool connectDeviceByIndex(uint8_t index);
  bool connected() const;
  void releaseConnectionForHotspot();
  void loop();

  uint8_t getScanCount() const { return scanCount_; }
  const BmsScanItem& getScanItem(uint8_t i) const { return scanItems_[i]; }
  const String& getConfiguredAddress() const { return configuredAddress_; }
  uint8_t getConfiguredAddressType() const { return configuredAddressType_; }
  void setConfiguredAddress(const String& mac, uint8_t addressType=BLE_ADDR_PUBLIC);
  uint8_t getScanAttempt() const { return scanAttempt_; }
  void setProtocol32S(bool enable){ protocol32S_=enable; protocolManager_.setProtocol32S(enable); }
  bool isProtocol32S() const { return protocol32S_; }
  const char* protocolName() const { return protocolManager_.protocolName(); }

private:
  NimBLEClient* client_;
  NimBLERemoteCharacteristic* ch_;
  NimBLERemoteCharacteristic* writeCh_;
  NimBLERemoteCharacteristic* notifyCh_;
  BmsProtocolManager protocolManager_;
  uint8_t counter_;
  uint8_t ttPollCounter_;
  uint8_t ttProbeCount_;
  uint32_t lastRequest_;
  uint32_t lastReconnectAttempt_;
  uint8_t scanCount_;
  uint8_t scanAttempt_;
  uint8_t configuredAddressType_;
  bool protocol32S_;
  String configuredAddress_;
  BmsScanItem scanItems_[BMS_SCAN_RESULT_MAX];

  static BmsBle* instance_;
  static void notifyCallback(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
  void handleNotification(const uint8_t*,size_t);
  void request(uint8_t);
  void requestAntStatus();
  void requestTtProbe();
  bool isCandidate(const NimBLEAdvertisedDevice*) const;
  void setStatus(BmsBootState state,const String& message);
};

// 保留旧名称兼容旧代码，不影响新的模块化命名。
using JkBle = BmsBle;
using JkScanItem = BmsScanItem;

#endif
