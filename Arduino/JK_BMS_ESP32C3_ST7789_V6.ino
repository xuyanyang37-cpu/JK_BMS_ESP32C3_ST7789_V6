

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


// ===== src/protocol/ant/AntProtocol.cpp =====
// 蚂蚁保护板协议预留模块；后续只在本目录增加真实帧解析。


// ===== src/protocol/jbd/JbdProtocol.cpp =====
// JBD 协议预留模块。


// ===== src/protocol/daly/DalyProtocol.cpp =====
// DALY 协议预留模块。


// ===== src/protocol/tt/TtProtocol.cpp =====
uint16_t TtProtocol::crc16(const uint8_t*p,size_t n){
  uint16_t c=0xFFFF;
  while(n--){ c^=*p++; for(uint8_t b=0;b<8;b++) c=(c&1)?uint16_t((c>>1)^0xA001):uint16_t(c>>1); }
  return c;
}
uint16_t TtProtocol::be16(const uint8_t*p){return uint16_t(p[0]<<8|p[1]);}
uint32_t TtProtocol::be32(const uint8_t*p){return (uint32_t(be16(p))<<16)|be16(p+2);}
int16_t TtProtocol::decodeCurrent(uint16_t v){
  return v<50000 ? int16_t(v) : int16_t(int32_t(v)-65536);
}
int TtProtocol::decodeTemp(uint16_t raw){
  if(raw<=200) return raw>100 ? 100-int(raw) : int(raw);
  int16_t s=(int16_t)raw;
  if(s>=-100&&s<=100) return s;
  return -128;
}
uint16_t TtProtocol::efSum(const uint8_t*p,size_t n){
  uint32_t s=0; for(size_t i=1;i+3<n;i++) s+=p[i];
  return uint16_t(0x10000-(s&0xFFFF));
}
bool TtProtocol::canHandle(const uint8_t*d,size_t n) const{
  return n>=3 && ((d[0]==0x01&&(d[1]==0x03||d[1]==0x01||(d[1]&0x80)))||d[0]==0xEF);
}
int TtProtocol::findFrameStart(const uint8_t*d,size_t n) const{
  if(!d) return -1;
  for(size_t i=0;i+1<n;i++)
    if((d[i]==0x01&&(d[i+1]==0x03||d[i+1]==0x01||(d[i+1]&0x80)))||d[i]==0xEF){ if(d[i]==0xEF && i+3<n) expectedLen_=7+d[i+3]; else if(i+2<n) expectedLen_=5+d[i+2]; return int(i); }
  return -1;
}
bool TtProtocol::parseFrame(const uint8_t*d,size_t n,BmsData&o){
  if(!d||n<5) return false;
  if(d[0]==0xEF) return parseEf(d,n,o);
  if(n>=3 && d[1]&0x80){expectedLen_=5;return false;}
  if(n<5 || d[2]>64) return false;
  size_t fl=5+d[2];
  if(n<fl) return false;
  uint16_t got=uint16_t(d[fl-2])|(uint16_t(d[fl-1])<<8);
  if(crc16(d,fl-2)!=got) return false;
  expectedLen_=fl;
  return parseModbus(d,fl,o);
}
bool TtProtocol::parseModbus(const uint8_t*f,size_t n,BmsData&o){
  if(n<5)return false;
  if(f[1]==0x03 && f[2]==24){
    char id[25]; memcpy(id,f+3,24); id[24]=0;
    for(int i=23;i>=0&&id[i]==' ';i--) id[i]=0;
    o.deviceName=String(id);
    o.mac=o.mac;
    o.online=true; o.updateMs=millis(); return false;
  }
  if(f[1]==0x01&&f[2]==7){\n    uint32_t ov=((uint32_t)(f[4]>>4)|((uint32_t)f[5]<<4)|((uint32_t)f[6]<<12))&0xFFFFF;\n    uint32_t uv=((uint32_t)f[7]|((uint32_t)f[8]<<8)|((uint32_t)(f[9]&0x0F)<<16))&0xFFFFF;\n    o.errors=(f[3]&0xFC)|((uint32_t)(f[4]&0x0F)<<8);\n    if(ov||uv)o.errors|=0x80000000UL;\n    o.online=true;o.updateMs=millis();return false;\n  }
    return false;
  }
  if(f[1]!=0x03 || f[2]!=58 || n!=63) return false;
  uint16_t tv=be16(f+3);
  if(tv==0xFFFD){o.valid=false;o.online=true;return false;}
  uint8_t cells=uint8_t(be16(f+5)); if(cells>20)cells=20;
  o.totalVoltage=tv*0.01f;
  o.cellCount=cells;
  o.soc=be16(f+7);
  o.remainingCapacityAh=be16(f+9)*0.01f;
  o.totalCapacityAh=o.totalCapacityAh>0?o.totalCapacityAh:o.remainingCapacityAh;
  o.current=decodeCurrent(be16(f+13))*0.01f;
  int t1=decodeTemp(be16(f+15)),t2=decodeTemp(be16(f+17)),t3=decodeTemp(be16(f+19));
  o.temperature1=(t1==-128)?0:t1;
  o.temperature2=(t2==-128)?0:t2;
  o.mosTemperature=(t3==-128)?0:t3;
  float minV=100,maxV=0;uint8_t minC=0,maxC=0;
  for(uint8_t i=0;i<JK_MAX_CELLS;i++)o.cellVoltage[i]=0;
  for(uint8_t i=0;i<cells;i++){
    float v=be16(f+21+i*2)*0.001f;o.cellVoltage[i]=v;
    if(v>=0.5f&&v<=6.0f){if(v<minV){minV=v;minC=i+1;}if(v>maxV){maxV=v;maxC=i+1;}}
  }
  o.minCellVoltage=(minV<100)?minV:0;o.maxCellVoltage=maxV;o.deltaCellVoltage=(maxV>0&&minV<100)?maxV-minV:0;o.minCell=minC;o.maxCell=maxC;
  o.power=o.totalVoltage*o.current;o.online=true;o.valid=true;o.updateMs=millis();
  if(o.energyConsumptionWhKm>1)o.remainingRangeKm=(o.remainingCapacityAh*o.totalVoltage)/o.energyConsumptionWhKm;
  return true;
}
bool TtProtocol::parseEf(const uint8_t*f,size_t n,BmsData&o){
  if(n<7||f[0]!=0xEF||f[n-1]!=0x16)return false;
  size_t fl=7+f[3];if(n<fl)return false;
  uint16_t got=uint16_t(f[fl-3])<<8|f[fl-2];
  if(efSum(f,fl)!=got)return false;
  expectedLen_=fl;
  if(f[2]==0xCA&&f[3]>=9){o.updateMs=millis();o.online=true;return false;}
  return false;
}
bool TtProtocol::buildCommand(uint8_t cmd,uint8_t,uint8_t out[20]){
  memset(out,0,20);
  if(cmd==0x03){uint8_t q[]={0x01,0x03,0x00,0x00,0x00,0x1D};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;expectedLen_=63;return true;}
  if(cmd==0x18){uint8_t q[]={0x01,0x03,0x03,0xE8,0x00,0x0C};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;return true;}
  if(cmd==0x01){uint8_t q[]={0x01,0x01,0x00,0x00,0x00,0x34};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;return true;}
  if(cmd==0xCA){uint8_t q[]={0xEF,0x01,0xCA,0x00,0xFF};memcpy(out,q,5);uint16_t c=efSum(out,7);out[5]=c>>8;out[6]=c&255;out[7]=0x16;return true;}
  return false;
}
size_t TtProtocol::commandLength(uint8_t cmd) const{return (cmd==0x03||cmd==0x18||cmd==0x01)?8:8;}


// ===== src/protocol/BmsProtocolManager.cpp =====
BmsProtocolManager::BmsProtocolManager():activeProtocol_(nullptr){}
void BmsProtocolManager::begin(bool protocol32S){jkProtocol_.setProtocol32S(protocol32S);activeProtocol_=&jkProtocol_;}
void BmsProtocolManager::setProtocol32S(bool enable){jkProtocol_.setProtocol32S(enable);}
bool BmsProtocolManager::isProtocol32S() const{return jkProtocol_.isProtocol32S();}
BmsProtocol* BmsProtocolManager::detectProtocol(const uint8_t*d,size_t n){
  if(jkProtocol_.canHandle(d,n)) return &jkProtocol_;
  if(antProtocol_.canHandle(d,n)) return &antProtocol_;
  if(jbdProtocol_.canHandle(d,n)) return &jbdProtocol_;
  if(dalyProtocol_.canHandle(d,n)) return &dalyProtocol_;
  if(ttProtocol_.canHandle(d,n)) return &ttProtocol_;
  return nullptr;
}
bool BmsProtocolManager::parseFrame(const uint8_t*d,size_t n,BmsData&o){
  BmsProtocol*p=detectProtocol(d,n); if(!p) return false;
  activeProtocol_=p; return activeProtocol_->parseFrame(d,n,o);
}
bool BmsProtocolManager::buildCommand(uint8_t c,uint8_t n,uint8_t out[20]){
  if(!activeProtocol_) activeProtocol_=&jkProtocol_;
  return activeProtocol_->buildCommand(c,n,out);
}
int BmsProtocolManager::findFrameStart(const uint8_t*d,size_t n){
  int best=-1; BmsProtocol* list[]={&jkProtocol_,&antProtocol_,&jbdProtocol_,&dalyProtocol_,&ttProtocol_};
  for(size_t i=0;i<sizeof(list)/sizeof(list[0]);i++){
    int s=list[i]->findFrameStart(d,n);
    if(s>=0&&(best<0||s<best)){ best=s; activeProtocol_=list[i]; }
  }
  return best;
}
size_t BmsProtocolManager::expectedFrameLength() const{return activeProtocol_?activeProtocol_->expectedFrameLength():300;}
const char* BmsProtocolManager::protocolName() const{return activeProtocol_?activeProtocol_->name():"NONE";}


// ===== src/ble/BmsBle.cpp =====
static const char* SERVICE="FFE0";
static const char* WRITE_CHAR="FFE1";
static const char* NOTIFY_CHAR="FFE2";
BmsBle* BmsBle::instance_=nullptr;

BmsBle::BmsBle()
  : client_(nullptr),ch_(nullptr),writeCh_(nullptr),notifyCh_(nullptr),
    counter_(0),lastRequest_(0),lastReconnectAttempt_(0),
    scanCount_(0),scanAttempt_(0),configuredAddressType_(BLE_ADDR_PUBLIC),
    protocol32S_(true),configuredAddress_("") {
  instance_=this;
}

bool BmsBle::begin(){
  NimBLEDevice::init("JK-C3-DISPLAY");
  NimBLEDevice::setPower(9);

  Preferences p;
  p.begin("jkcfg", true);
  configuredAddress_=p.getString("mac", "");
  configuredAddressType_=p.getUChar("mactype", BLE_ADDR_PUBLIC);
  protocol32S_=p.getBool("32s", true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm", 100.0f);
  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;
  p.end();

  protocolManager_.begin(protocol32S_);
  return true;
}

void BmsBle::setConfiguredAddress(const String& mac, uint8_t addressType){
  configuredAddress_=mac;
  configuredAddressType_=addressType;

  Preferences p;
  p.begin("jkcfg", false);
  p.putString("mac", configuredAddress_);
  p.putUChar("mactype", configuredAddressType_);
  p.end();
}

void BmsBle::setStatus(BmsBootState state, const String& message){
  g_bmsData.bootState=state;
  g_bmsData.statusMessage=message;
  g_bmsData.scanAttempt=scanAttempt_;
}

bool BmsBle::isCandidate(const NimBLEAdvertisedDevice* d) const{
  String n=d->getName().c_str();
  n.toUpperCase();
  return d->isAdvertisingService(NimBLEUUID(SERVICE)) ||
         n.indexOf("JK")>=0 || n.indexOf("JIKONG")>=0 || n.indexOf("BMS")>=0;
}

uint8_t BmsBle::scanDevices(uint32_t sec){
  scanCount_=0;
  NimBLEScan* s=NimBLEDevice::getScan();
  s->setActiveScan(true);
  s->setInterval(80);
  s->setWindow(60);

  NimBLEScanResults r=s->getResults(sec*1000,false);
  for(uint32_t i=0;(uint32_t)i<r.getCount() && scanCount_<BMS_SCAN_RESULT_MAX;i++){
    const NimBLEAdvertisedDevice* d=r.getDevice(i);
    if(!isCandidate(d)) continue;

    scanItems_[scanCount_].address=d->getAddress().toString().c_str();
    scanItems_[scanCount_].addressType=d->getAddressType();
    scanItems_[scanCount_].name=d->getName().c_str();
    if(scanItems_[scanCount_].name.length()==0) scanItems_[scanCount_].name="JK-BMS";
    scanItems_[scanCount_].rssi=d->getRSSI();
    Serial.printf("JK BLE: %s type=%u RSSI=%d\n",
                  scanItems_[scanCount_].address.c_str(),
                  scanItems_[scanCount_].addressType,
                  scanItems_[scanCount_].rssi);
    scanCount_++;
  }

  s->stop();
  s->clearResults();
  return scanCount_;
}

bool BmsBle::scanAndConnect(uint32_t sec, uint8_t attemptOverride){
  if(connected() && g_bmsData.valid) return true;

  if(connected()){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    g_bmsData.online=false; g_bmsData.valid=false;
  }

  if(attemptOverride>=1 && attemptOverride<=3) scanAttempt_=attemptOverride;
  else { scanAttempt_++; if(scanAttempt_>3) scanAttempt_=3; }

  g_bmsData.scanAttempt=scanAttempt_;
  setStatus(BOOT_SCANNING,"扫描蓝牙电池 "+String(scanAttempt_)+"/3");
  scanDevices(sec);

  if(configuredAddress_.length()){
    if(connectByAddress(configuredAddress_,configuredAddressType_)) return true;
  }

  if(scanCount_==0){
    setStatus(BOOT_SCANNING,"第 "+String(scanAttempt_)+"/3 次未找到JK电池");
    return false;
  }

  uint8_t best=0;
  for(uint8_t i=1;i<scanCount_;i++)
    if(scanItems_[i].rssi>scanItems_[best].rssi) best=i;

  return connectDeviceByIndex(best);
}

bool BmsBle::connectDeviceByIndex(uint8_t index){
  if(index>=scanCount_) return false;
  return connectByAddress(scanItems_[index].address,scanItems_[index].addressType);
}

bool BmsBle::connectByAddress(const String& address,uint8_t addressType){
  if(address.length()==0) return false;

  NimBLEAddress addr(address.c_str(),addressType);
  if(client_){
    if(client_->isConnected()) client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
  }

  Serial.printf("JK BLE: connect %s type=%u (%s)\n",
                address.c_str(),addressType,
                addressType==BLE_ADDR_PUBLIC?"PUBLIC":"RANDOM");
  setStatus(BOOT_CONNECTING,"连接 "+address);

  client_=NimBLEDevice::createClient();
  if(!client_){
    setStatus(BOOT_SCANNING,"创建BLE客户端失败");
    return false;
  }

  client_->setConnectTimeout(5000);
  if(!client_->connect(addr)){
    setStatus(BOOT_SCANNING,"连接失败");
    g_bmsData.online=false;
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    return false;
  }

  NimBLERemoteService* s=client_->getService(NimBLEUUID(SERVICE));
  if(!s){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    setStatus(BOOT_SCANNING,"找不到FFE0服务");
    return false;
  }

  NimBLERemoteCharacteristic* ffe1=s->getCharacteristic(NimBLEUUID(WRITE_CHAR));
  NimBLERemoteCharacteristic* ffe2=s->getCharacteristic(NimBLEUUID(NOTIFY_CHAR));

  writeCh_=nullptr; notifyCh_=nullptr;
  if(ffe1 && (ffe1->canWriteNoResponse() || ffe1->canWrite())) writeCh_=ffe1;
  if(ffe1 && (ffe1->canNotify() || ffe1->canIndicate())) notifyCh_=ffe1;
  if(!notifyCh_ && ffe2 && (ffe2->canNotify() || ffe2->canIndicate())) notifyCh_=ffe2;
  if(!writeCh_ && ffe2 && (ffe2->canWriteNoResponse() || ffe2->canWrite())) writeCh_=ffe2;

  Serial.printf("JK BLE chars: FFE1 write=%d notify=%d; FFE2 write=%d notify=%d\n",
                ffe1 ? (int)(ffe1->canWriteNoResponse() || ffe1->canWrite()) : 0,
                ffe1 ? (int)(ffe1->canNotify() || ffe1->canIndicate()) : 0,
                ffe2 ? (int)(ffe2->canWriteNoResponse() || ffe2->canWrite()) : 0,
                ffe2 ? (int)(ffe2->canNotify() || ffe2->canIndicate()) : 0);

  if(!writeCh_){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    setStatus(BOOT_SCANNING,"FFE1不可写");
    return false;
  }

  if(!notifyCh_){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    setStatus(BOOT_SCANNING,"FFE1/FFE2无通知能力");
    return false;
  }

  ch_=writeCh_;
  bool subscribed=false;
  if(notifyCh_->canNotify()) subscribed=notifyCh_->subscribe(true,notifyCallback);
  else if(notifyCh_->canIndicate()) subscribed=notifyCh_->subscribe(false,notifyCallback);

  if(!subscribed){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr; writeCh_=nullptr; notifyCh_=nullptr;
    setStatus(BOOT_SCANNING,"FFE1/FFE2通知订阅失败");
    return false;
  }

  // 只有扫描得到的设备地址类型才写入配置。
  // 这样随机地址设备重启后也能用正确的 address type 连接。
  setConfiguredAddress(address,addressType);

  g_bmsData.mac=address;
  g_bmsData.online=true;
  g_bmsData.bootState=BOOT_CONNECTED;
  g_bmsData.statusMessage="已连接JK电池";
  g_bmsData.scanAttempt=scanAttempt_;

  request(0x96);
  delay(100);
  request(0x97);
  // ANT-BMS 与 JK 共用常见 FFE0/FFE1 服务；同时发一次 ANT 状态请求，收到 7E A1 后自动切换解析器。
  delay(100);
  requestAntStatus();
  delay(100);
  requestTtProbe();
  lastRequest_=millis();
  return true;
}

void BmsBle::notifyCallback(NimBLERemoteCharacteristic*,uint8_t* d,size_t n,bool){
  if(instance_) instance_->handleNotification(d,n);
}

void BmsBle::handleNotification(const uint8_t* d,size_t n){
  Serial.printf("BMS RX notify len=%u: ",(unsigned)n);
  size_t dump=n<24?n:24;
  for(size_t i=0;i<dump;i++) Serial.printf("%02X ",d[i]);
  if(n>dump) Serial.print("...");
  Serial.println();

  static uint8_t rx[700];
  static size_t len=0;
  if(n>sizeof(rx) || len+n>sizeof(rx)){len=0;return;}
  memcpy(rx+len,d,n);
  len+=n;

  while(len>=5){
    // 协议管理器负责寻找帧头，BLE 层不再写死 JK 的 55 AA EB 90。
    int start=protocolManager_.findFrameStart(rx,len);
    if(start<0){
      // 保留最后 3 字节，防止下一次 Notify 才收到跨包帧头。
      if(len>3){
        memmove(rx,rx+len-3,3);
        len=3;
      }
      break;
    }

    if(start>0){
      memmove(rx,rx+start,len-start);
      len-=start;
      if(len<4) break;
    }

    size_t expected=protocolManager_.expectedFrameLength();
    if(len>=expected){
      if(protocolManager_.parseFrame(rx,expected,g_bmsData)) g_bmsData.online=true;
      memmove(rx,rx+expected,len-expected);
      len-=expected;
      continue;
    }
    break;
  }
}

void BmsBle::requestAntStatus(){
  if(!writeCh_ && !ch_) return;
  NimBLERemoteCharacteristic* writer=writeCh_ ? writeCh_ : ch_;
  uint8_t f[10]={0x7E,0xA1,0x01,0x00,0x00,0xBE,0x00,0x00,0xAA,0x55};
  uint16_t crc=0xFFFF;
  for(int i=1;i<=5;i++){ crc^=f[i]; for(uint8_t b=0;b<8;b++) crc=(crc&1)?(crc>>1)^0xA001:(crc>>1); }
  f[6]=uint8_t(crc); f[7]=uint8_t(crc>>8);
  if(writer->canWriteNoResponse()) writer->writeValue(f,10,false);
  else if(writer->canWrite()) writer->writeValue(f,10,true);
}

void BmsBle::requestTtProbe(){
  if(!writeCh_ && !ch_) return;
  NimBLERemoteCharacteristic* writer=writeCh_ ? writeCh_ : ch_;
  uint8_t f[8]={0x01,0x03,0x00,0x00,0x00,0x1D,0x00,0x00};
  uint16_t crc=0xFFFF; for(int i=0;i<6;i++){crc^=f[i];for(uint8_t b=0;b<8;b++)crc=(crc&1)?uint16_t((crc>>1)^0xA001):uint16_t(crc>>1);}
  f[6]=uint8_t(crc);f[7]=uint8_t(crc>>8);
  if(writer->canWriteNoResponse()) writer->writeValue(f,8,false); else if(writer->canWrite()) writer->writeValue(f,8,true);
}

void BmsBle::request(uint8_t cmd){
  if(!writeCh_ && !ch_) return;
  NimBLERemoteCharacteristic* writer=writeCh_ ? writeCh_ : ch_;
  uint8_t f[20];
  if(!protocolManager_.buildCommand(cmd,counter_++,f)) return;
  size_t n=20;
  if(String(protocolManager_.protocolName())=="TT") n=(cmd==0x03||cmd==0x18||cmd==0x01)?8:8;
  if(writer->canWriteNoResponse()) writer->writeValue(f,n,false);
  else if(writer->canWrite()) writer->writeValue(f,n,true);
}

void BmsBle::loop(){
  if(!connected()){
    g_bmsData.online=false;

    if(g_bmsData.bootState==BOOT_CONNECTED)
      setStatus(BOOT_SCANNING,"蓝牙已断开");

    if(g_bmsData.bootState!=BOOT_HOTSPOT &&
       millis()-lastReconnectAttempt_>=15000){
      lastReconnectAttempt_=millis();
      uint8_t reconnectAttempt=scanAttempt_;
      if(reconnectAttempt<1 || reconnectAttempt>3) reconnectAttempt=1;
      if(!scanAndConnect(3,reconnectAttempt)) g_bmsData.online=false;
    }
    return;
  }

  if(String(protocolManager_.protocolName())=="TT"){
    if(millis()-lastRequest_>=250){
      request(0x03);
      lastRequest_=millis();
    }
  } else {
    uint32_t requestInterval=g_bmsData.valid?5000UL:1500UL;
    if(millis()-lastRequest_>requestInterval){ request(0x96); requestAntStatus(); requestTtProbe(); lastRequest_=millis(); }
  }
}

bool BmsBle::connected() const{
  return client_ && client_->isConnected();
}

void BmsBle::releaseConnectionForHotspot(){
  if(client_){
    if(client_->isConnected()) client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
  }

  ch_=nullptr;
  writeCh_=nullptr;
  notifyCh_=nullptr;
  g_bmsData.online=false;
  g_bmsData.valid=false;

  NimBLEScan* s=NimBLEDevice::getScan();
  if(s){
    s->stop();
    s->clearResults();
  }
}
