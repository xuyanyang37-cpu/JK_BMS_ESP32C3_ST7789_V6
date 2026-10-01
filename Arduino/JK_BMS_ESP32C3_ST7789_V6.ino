

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
