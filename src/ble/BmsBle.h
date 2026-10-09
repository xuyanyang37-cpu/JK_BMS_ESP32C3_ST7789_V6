#ifndef BMS_BLE_H
#define BMS_BLE_H
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../BmsData.h"
#include "../protocol/BmsProtocolManager.h"
#include "../protocol/yanyang_protocol.h"

#define BMS_SCAN_RESULT_MAX 1

struct BmsScanItem {
  // 只保留RSSI最强的一个候选，固定数组避免String堆分配/碎片。
  char address[18] = {0};
  uint8_t addressType=BLE_ADDR_PUBLIC;
  char name[24] = {0};
  int rssi=-127;
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
  // NimBLE扫描回调：逐个广播结果处理，只保留RSSI最强候选。
  void onScanResult(const NimBLEAdvertisedDevice* device);
  void releaseConnectionForHotspot();
  void loop();
  // setup()完成首次有效连接后调用，之后断线恢复才启用“连续3次失败进入热点”。
  void setRuntimeRecoveryEnabled(bool enable){ runtimeRecoveryEnabled_=enable; }

  uint8_t getScanCount() const { return scanCount_; }
  const BmsScanItem& getScanItem(uint8_t i) const { return scanItems_[i]; }
  const String& getConfiguredAddress() const { return configuredAddress_; }
  uint8_t getConfiguredAddressType() const { return configuredAddressType_; }
  void setConfiguredAddress(const String& mac, uint8_t addressType=BLE_ADDR_PUBLIC);
  uint8_t getScanAttempt() const { return scanAttempt_; }
  void setProtocol32S(bool enable){ protocol32S_=enable; protocolManager_.setProtocol32S(enable); }
  bool isProtocol32S() const { return protocol32S_; }
  void setPreferredProtocol(const String& name);
  String getPreferredProtocol() const;
  const char* protocolName() const { return yanyangMode_ ? "YANYANG" : protocolManager_.protocolName(); }

private:
  NimBLEClient* client_;
  NimBLERemoteCharacteristic* ch_;
  NimBLERemoteCharacteristic* writeCh_;
  NimBLERemoteCharacteristic* notifyCh_;
  BmsProtocolManager protocolManager_;
  YanyangProtocolDecoder yanyangDecoder_;
  bool yanyangMode_;
  uint8_t counter_;
  uint32_t lastRequest_;
  uint32_t lastReconnectAttempt_;
  uint32_t recoveryVerifyStart_;
  uint8_t recoveryFailures_;
  bool runtimeRecoveryEnabled_;
  uint8_t scanCount_;
  uint8_t scanAttempt_;
  uint8_t configuredAddressType_;
  bool protocol32S_;
  String configuredAddress_;
  BmsScanItem scanItems_[BMS_SCAN_RESULT_MAX];

  static BmsBle* instance_;
  static void notifyCallback(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
  static void onYanyangStatus(const BmsData& data);
  static void onYanyangDeviceInfo(const char* hardwareVersion, const char* softwareVersion);
  void handleNotification(const uint8_t*,size_t);
  void request(uint8_t);
  void requestAntStatus();
  void requestTtProbe();
  void requestYanyangStatus();
  bool isCandidate(const NimBLEAdvertisedDevice*) const;
  void setStatus(BmsBootState state,const String& message);
};

// 保留旧名称兼容旧代码，不影响新的模块化命名。
using JkBle = BmsBle;
using JkScanItem = BmsScanItem;

#endif
