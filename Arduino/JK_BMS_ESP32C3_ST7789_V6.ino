

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


// ===== src/font/FontGB2312.h =====
#ifndef FONT_GB2312_H
#define FONT_GB2312_H
// V6 编译兼容版：保留原接口。
// 若项目已有完整 GB2312 字库，可直接替换本文件和 FontGB2312.cpp。
namespace FontGB2312 {
void drawText(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font=1);
void drawCenterString(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font=1);
}
#endif


// ===== src/display/Display.h =====
#ifndef DISPLAY_H
#define DISPLAY_H

class Display {
public:
  void begin();
  void update(const BmsData& d);

private:
  TFT_eSPI tft_;
  TFT_eSprite socSprite_{&tft_};
  TFT_eSprite leftInfoSprite_{&tft_};
  TFT_eSprite rowSprite_{&tft_};
  TFT_eSprite barSprite_{&tft_};

  bool initialized_ = false;
  bool firstDashboard_ = true;
  bool scanScreenInitialized_ = false;
  BmsBootState lastBootState_ = BOOT_START;

  BmsData lastData_{};

  void drawFullPage(const BmsData& d);
  void drawScanningScreen(const BmsData& d, bool force);
  void drawDashboard(const BmsData& d, bool force);
  void drawSoc(const BmsData& d);
  void drawVoltage(const BmsData& d);
  void drawCurrent(const BmsData& d);
  void drawPower(const BmsData& d);
  void drawTemperature(const BmsData& d);
  void drawRange(const BmsData& d);
  void drawSocBar(const BmsData& d);

  bool changed(float a, float b, float eps) const;
};

#endif


// ===== src/web/WebConfig.h =====
#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H
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


// ===== src/protocol/jk/Jk02_24S.cpp =====
// JK02_24S 模型参数集中放在这里，便于后续根据实机帧继续修正。


// ===== src/protocol/jk/Jk02_32S.cpp =====
// JK02_32S 模型参数集中放在这里，便于后续根据实机帧继续修正。


// ===== src/protocol/jk/JkProtocol.cpp =====
JkProtocol::JkProtocol():protocol32S_(true){}
bool JkProtocol::canHandle(const uint8_t*p,size_t n) const{
  return p && n>=4 && p[0]==0x55 && p[1]==0xAA && p[2]==0xEB && p[3]==0x90;
}
int JkProtocol::findFrameStart(const uint8_t*p,size_t n) const{
  if(!p || n<4) return -1;
  for(size_t i=0;i+4<=n;i++)
    if(p[i]==0x55 && p[i+1]==0xAA && p[i+2]==0xEB && p[i+3]==0x90) return (int)i;
  return -1;
}
uint16_t JkProtocol::u16le(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
uint32_t JkProtocol::u32le(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int16_t JkProtocol::s16le(const uint8_t*p){return (int16_t)u16le(p);}
bool JkProtocol::validCrc(const uint8_t*p,size_t n){
  if(n<5) return false;
  uint8_t s=0; for(size_t i=0;i+1<n;i++) s+=p[i];
  return s==p[n-1];
}
int JkProtocol::detectOffset(const uint8_t*,size_t) const{
  return protocol32S_ ? Jk02_32S::DATA_OFFSET : Jk02_24S::DATA_OFFSET;
}
bool JkProtocol::parseMainFrame(const uint8_t*p,size_t n,BmsData&o){
  int off=detectOffset(p,n);
  if(n<(size_t)(184+off)) return false;
  uint32_t mask=u32le(p+54+off);
  uint8_t maxCells=protocol32S_ ? Jk02_32S::MAX_CELLS : Jk02_24S::MAX_CELLS;
  uint8_t cells=0;
  for(uint8_t i=0;i<maxCells;i++) if(mask & (1UL<<i)) cells++;
  if(cells==0) return false;
  if(cells>JK_MAX_CELLS) cells=JK_MAX_CELLS;
  float minV=100,maxV=0; uint8_t minC=0,maxC=0;
  for(uint8_t i=0;i<JK_MAX_CELLS;i++) o.cellVoltage[i]=0;
  for(uint8_t i=0;i<cells;i++){
    size_t pos=6+i*2;
    if(pos+1>=n) break;
    float v=u16le(p+pos)*0.001f;
    o.cellVoltage[i]=v;
    if(v>0.5f&&v<6.0f){if(v<minV){minV=v;minC=i+1;} if(v>maxV){maxV=v;maxC=i+1;}}
  }
  o.cellCount=cells;
  o.minCellVoltage=minV<100?minV:0;
  o.maxCellVoltage=maxV;
  o.deltaCellVoltage=(maxV>0&&minV<100)?maxV-minV:0;
  o.minCell=minC; o.maxCell=maxC;
  o.totalVoltage=u32le(p+118+off)*0.001f;
  o.current=(int32_t)u32le(p+126+off)*0.001f;
  o.power=o.totalVoltage*o.current;
  o.temperature1=s16le(p+130+off)*0.1f;
  o.temperature2=s16le(p+132+off)*0.1f;
  o.mosTemperature=s16le(p+(off?112+off:134))*0.1f;
  if(protocol32S_) o.errors=u32le(p+134+off); else o.errors=u16le(p+136);
  o.balancingCurrent=u16le(p+138+off)*0.001f;
  o.balancing=p[140+off]!=0;
  o.soc=p[141+off]; if(o.soc>100.0f) o.soc=100.0f;
  o.remainingCapacityAh=u32le(p+142+off)*0.001f;
  o.totalCapacityAh=u32le(p+146+off)*0.001f;
  if(o.energyConsumptionWhKm>1.0f&&o.totalVoltage>0.1f)
    o.remainingRangeKm=(o.remainingCapacityAh*o.totalVoltage)/o.energyConsumptionWhKm;
  else o.remainingRangeKm=0;
  o.charging=p[166+off]!=0; o.discharging=p[167+off]!=0;
  o.balancing=o.balancing||p[169+off]!=0; o.heating=p[183+off]!=0;
  o.valid=o.totalVoltage>0.1f; o.online=o.valid; o.updateMs=millis();
  return o.valid;
}
bool JkProtocol::parseFrame(const uint8_t*p,size_t n,BmsData&o){
  if(!canHandle(p,n)||!validCrc(p,n)) return false;
  return p[4]==0x02 ? parseMainFrame(p,n,o) : false;
}
bool JkProtocol::buildCommand(uint8_t cmd,uint8_t counter,uint8_t out[20]){
  (void)counter; memset(out,0,20);
  out[0]=0xAA;out[1]=0x55;out[2]=0x90;out[3]=0xEB;out[4]=cmd;out[5]=0;
  uint8_t s=0; for(int i=0;i<19;i++) s+=out[i]; out[19]=s; return true;
}
