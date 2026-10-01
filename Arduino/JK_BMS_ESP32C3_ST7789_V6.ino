

// ===== 单文件合并：基础定义/接口 =====
#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include "esp_system.h"


// ===== src/BmsData.h =====
enum BmsBootState {
  BOOT_START = 0,
  BOOT_SCANNING = 1,
  BOOT_CONNECTING = 2,
  BOOT_CONNECTED = 3,
  BOOT_HOTSPOT = 4
};

struct BmsData {
  bool online=false, valid=false;
  uint8_t cellCount=0;
  float totalVoltage=0, current=0, power=0, soc=0;
  float minCellVoltage=0, maxCellVoltage=0, deltaCellVoltage=0;
  uint8_t minCell=0, maxCell=0;
  float cellVoltage[JK_MAX_CELLS]={0};
  float temperature1=0, temperature2=0, mosTemperature=0;
  float remainingCapacityAh=0, totalCapacityAh=0, balancingCurrent=0;
  // 剩余里程 = 剩余容量(Ah) × 电压(V) ÷ 每公里耗电(Wh/km)
  float remainingRangeKm=0;
  float energyConsumptionWhKm=100.0f;
  bool charging=false, discharging=false, balancing=false, heating=false;
  uint32_t errors=0, updateMs=0;

  String mac, deviceName, softwareVersion, hardwareVersion;

  BmsBootState bootState=BOOT_START;
  uint8_t scanAttempt=0;
  uint8_t scanMax=3;
  bool hotspot=false;
  String hotspotIp;
  String statusMessage="正在启动";
};

extern BmsData g_bmsData;

// ===== src/tft_setup.h =====
// ESP32-C3 + ST7789 1.9 inch 320x170

// ===== src/protocol/BmsProtocol.h =====
class BmsProtocol {
public:
  virtual ~BmsProtocol() {}
  virtual const char* name() const=0;
  virtual bool canHandle(const uint8_t*,size_t) const=0;
  virtual int findFrameStart(const uint8_t*,size_t) const=0;
  virtual bool parseFrame(const uint8_t*,size_t,BmsData&)=0;
  virtual bool buildCommand(uint8_t,uint8_t,uint8_t[20])=0;
  virtual size_t expectedFrameLength() const=0;
};

// ===== src/protocol/jk/Jk02_24S.h =====
class Jk02_24S {
public:
  static const uint8_t MAX_CELLS=24;
  static const int DATA_OFFSET=0;
  static const size_t FRAME_LENGTH=300;
  static const char* name(){return "JK02_24S";}
};

// ===== src/protocol/jk/Jk02_32S.h =====
class Jk02_32S {
public:
  static const uint8_t MAX_CELLS=32;
  static const int DATA_OFFSET=32;
  static const size_t FRAME_LENGTH=300;
  static const char* name(){return "JK02_32S";}
};

// ===== src/protocol/jk/JkProtocol.h =====
class JkProtocol : public BmsProtocol {
public:
  static const size_t FRAME_MAX=360;
  JkProtocol();
  void setProtocol32S(bool enable){protocol32S_=enable;}
  bool isProtocol32S() const {return protocol32S_;}
  const char* name() const override {return protocol32S_ ? Jk02_32S::name() : Jk02_24S::name();}
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override {return 300;}
private:
  bool protocol32S_;
  static uint16_t u16le(const uint8_t*);
  static uint32_t u32le(const uint8_t*);
  static int16_t s16le(const uint8_t*);
  static bool validCrc(const uint8_t*,size_t);
  int detectOffset(const uint8_t*,size_t) const;
  bool parseMainFrame(const uint8_t*,size_t,BmsData&);
};

// ===== 以下为各模块实现，后续提交继续追加 =====


// ===== src/protocol/BmsProtocolManager.h =====
class BmsProtocolManager {
public:
  BmsProtocolManager();
  void begin(bool protocol32S);
  void setProtocol32S(bool enable);
  bool isProtocol32S() const;
  bool parseFrame(const uint8_t*,size_t,BmsData&);
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]);
  int findFrameStart(const uint8_t*,size_t);
  size_t expectedFrameLength() const;
  const char* protocolName() const;
private:
  JkProtocol jkProtocol_;
  AntProtocol antProtocol_;
  JbdProtocol jbdProtocol_;
  DalyProtocol dalyProtocol_;
  TtProtocol ttProtocol_;
  BmsProtocol* activeProtocol_;
  BmsProtocol* detectProtocol(const uint8_t*,size_t);
};


// ===== src/protocol/ant/AntProtocol.h =====
class AntProtocol : public BmsProtocol {
public:
  const char* name() const override { return "ANT"; }
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override { return frameLength_; }
private:
  mutable size_t frameLength_=0;
  static uint16_t crc16(const uint8_t*,size_t);
  static uint16_t u16le(const uint8_t*);
  static uint32_t u32le(const uint8_t*);
  static int16_t s16le(const uint8_t*);
  static bool validFrame(const uint8_t*,size_t);
};


// ===== src/protocol/jbd/JbdProtocol.h =====
class JbdProtocol : public BmsProtocol {
public:
  const char* name() const override{return "JBD";}
  bool canHandle(const uint8_t*,size_t) const override{return false;}
  int findFrameStart(const uint8_t*,size_t) const override{return -1;}
  bool parseFrame(const uint8_t*,size_t,BmsData&) override{return false;}
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override{return false;}
  size_t expectedFrameLength() const override{return 0;}
};


// ===== src/protocol/daly/DalyProtocol.h =====
class DalyProtocol : public BmsProtocol {
public:
 const char* name() const override{return "DALY";}
 bool canHandle(const uint8_t*,size_t) const override{return false;}
 int findFrameStart(const uint8_t*,size_t) const override{return -1;}
 bool parseFrame(const uint8_t*,size_t,BmsData&) override{return false;}
 bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override{return false;}
 size_t expectedFrameLength() const override{return 0;}
};


// ===== src/protocol/tt/TtProtocol.h =====
class TtProtocol : public BmsProtocol {
public:
  const char* name() const override { return "TT"; }
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override { return expectedLen_; }
  size_t commandLength(uint8_t cmd) const;
private:
  mutable size_t expectedLen_=63;
  static uint16_t crc16(const uint8_t*,size_t);
  static uint16_t be16(const uint8_t*);
  static uint32_t be32(const uint8_t*);
  static int16_t decodeCurrent(uint16_t);
  static int decodeTemp(uint16_t);
  static uint16_t efSum(const uint8_t*,size_t);
  bool parseModbus(const uint8_t*,size_t,BmsData&);
  bool parseEf(const uint8_t*,size_t,BmsData&);
};


// ===== src/ble/BmsBle.h =====
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
