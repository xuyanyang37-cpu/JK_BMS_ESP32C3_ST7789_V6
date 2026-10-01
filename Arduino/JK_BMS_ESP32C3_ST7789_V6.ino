

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


// ===== src/font/FontGB2312.cpp =====
namespace FontGB2312 {
void drawText(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font){
  s.setTextColor(fg,bg);
  s.drawString(text,x,y,font);
}
void drawCenterString(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font){
  s.setTextColor(fg,bg);
  s.drawCentreString(text,x,y,font);
}
}


// ===== src/display/Display.cpp =====
/*
 * JK BMS + ST7789 1.9" / 320x170
 * UI 2.0 - 按设计图重新做像素级布局。
 *
 * 主界面：
 *   左：SOC
 *   左下：温度 / 单体压差 + 剩余容量
 *   右：电压 / 电流 / 功率 / 剩余里程
 *   底：SOC 渐变条
 *
 * 主界面只显示电池运行数据，不显示连接状态文字。
 */

namespace {
  // 设计图的深蓝色圆角卡片。
  static const uint16_t UI_PANEL      = 0x0948;
  static const uint16_t UI_PANEL_DARK = 0x0127;

  static const uint16_t UI_WHITE   = TFT_WHITE;
  static const uint16_t UI_VOLTAGE = 0xFFE0; // 明黄色
  static const uint16_t UI_CURRENT = 0x07E0; // 亮绿色
  static const uint16_t UI_POWER   = 0xFCA8; // 柔和红色
  static const uint16_t UI_RANGE   = 0x2F3C; // 青色
  static const uint16_t UI_TEMP    = 0x07E0;

  // 设计图：低电量可调颜色阈值。
  static const float SOC_WARN_THRESHOLD = 30.0f;
  static const float SOC_CRITICAL_THRESHOLD = 15.0f;

  // 16bit RGB565 颜色线性插值。
  static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t percent) {
    if (percent > 100) percent = 100;

    uint8_t ar = (a >> 11) & 0x1F;
    uint8_t ag = (a >> 5)  & 0x3F;
    uint8_t ab = a & 0x1F;

    uint8_t br = (b >> 11) & 0x1F;
    uint8_t bg = (b >> 5)  & 0x3F;
    uint8_t bb = b & 0x1F;

    uint8_t rr = ar + (((int16_t)br - (int16_t)ar) * percent) / 100;
    uint8_t rg = ag + (((int16_t)bg - (int16_t)ag) * percent) / 100;
    uint8_t rb = ab + (((int16_t)bb - (int16_t)ab) * percent) / 100;

    return ((uint16_t)rr << 11) | ((uint16_t)rg << 5) | rb;
  }

  static uint16_t socColor(float soc) {
    if (soc <= SOC_CRITICAL_THRESHOLD) return TFT_RED;
    if (soc <= SOC_WARN_THRESHOLD) return TFT_YELLOW;
    return UI_WHITE;
  }

  static void drawPanel(TFT_eSprite& sprite,
                        int16_t x, int16_t y,
                        int16_t w, int16_t h) {
    sprite.fillRoundRect(x, y, w, h, 7, UI_PANEL);
  }

  static void drawMetricRow(TFT_eSprite& sprite,
                            char icon,
                            const char* label,
                            const String& value,
                            uint16_t color) {
    sprite.fillSprite(TFT_BLACK);
    sprite.fillRoundRect(0, 0, 168, 27, 7, UI_PANEL);

    sprite.drawCircle(15, 13, 11, color);
    sprite.setTextColor(color, UI_PANEL);
    sprite.drawCentreString(String(icon), 15, 5, 2);

    FontGB2312::drawText(sprite, 31, 5,
                         String(label), color, UI_PANEL, 1);

    sprite.setTextColor(color, UI_PANEL);
    sprite.drawRightString(value, 164, 5, 2);
  }
}

bool Display::changed(float a, float b, float eps) const {
  return fabsf(a - b) >= eps;
}

void Display::begin() {
  // ST7789 初始化期间保持背光关闭，避免初始化过程中的白屏/闪屏。
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  // 初始化 ST7789 控制器。
  tft_.init();
  tft_.setRotation(1);

  // 控制器初始化完成后先清黑屏，再开启背光。
  tft_.fillScreen(TFT_BLACK);
  delay(30);
  digitalWrite(TFT_BL, HIGH);

  socSprite_.setColorDepth(16);
  leftInfoSprite_.setColorDepth(16);
  rowSprite_.setColorDepth(16);
  barSprite_.setColorDepth(16);

  socSprite_.createSprite(140, 78);
  leftInfoSprite_.createSprite(140, 67);
  rowSprite_.createSprite(168, 27);
  barSprite_.createSprite(312, 11);

  socSprite_.fillSprite(TFT_BLACK);
  leftInfoSprite_.fillSprite(TFT_BLACK);
  rowSprite_.fillSprite(TFT_BLACK);
  barSprite_.fillSprite(TFT_BLACK);

  initialized_ = true;
  firstDashboard_ = true;
  drawFullPage(g_bmsData);
}

void Display::update(const BmsData& d) {
  if (!initialized_) return;

  // 扫描阶段单独处理：
  // 第一次进入扫描页才整页绘制，后续 1/3 -> 2/3 -> 3/3
  // 只刷新进度条和次数，不再 fillScreen()/整页 pushSprite()。
  if (d.bootState == BOOT_SCANNING || d.bootState == BOOT_START) {
    bool force = !scanScreenInitialized_ || lastBootState_ != d.bootState;
    drawScanningScreen(d, force);
    scanScreenInitialized_ = true;
    lastBootState_ = d.bootState;
    lastData_ = d;
    return;
  }

  // 离开扫描页时，允许下一次扫描重新初始化静态区域。
  scanScreenInitialized_ = false;

  if (d.bootState != lastBootState_ ||
      d.hotspot != lastData_.hotspot) {
    drawFullPage(d);
    lastBootState_ = d.bootState;
    lastData_ = d;
    return;
  }

  if (d.bootState != BOOT_CONNECTED && !d.online) {
    if (d.statusMessage != lastData_.statusMessage ||
        d.scanAttempt != lastData_.scanAttempt ||
        d.hotspotIp != lastData_.hotspotIp ||
        d.mac != lastData_.mac) {
      drawFullPage(d);
      lastData_ = d;
    }
    return;
  }

  drawDashboard(d, firstDashboard_);
  firstDashboard_ = false;
  lastData_ = d;
}

void Display::drawScanningScreen(const BmsData& d, bool force) {
  // 只在第一次进入扫描页时画固定内容。
  if (force) {
    tft_.fillScreen(TFT_BLACK);

    // 扫描页文字统一绘制到 Sprite，不直接绘制到 tft_。
    TFT_eSprite scanSprite(&tft_);
    scanSprite.setColorDepth(16);

    if (scanSprite.createSprite(320, 60)) {
      scanSprite.fillSprite(TFT_BLACK);

      FontGB2312::drawCenterString(scanSprite, 160, 7,
                                   "连接电池",
                                   TFT_CYAN, TFT_BLACK, 2);

      FontGB2312::drawCenterString(scanSprite, 160, 43,
                                   "扫描蓝牙电池",
                                   TFT_WHITE, TFT_BLACK, 1);

      scanSprite.pushSprite(0, 0);
      scanSprite.deleteSprite();
    }

    // 进度条外框
    tft_.drawRoundRect(35, 72, 250, 18, 5, TFT_DARKGREY);

    // 底部文字也使用 Sprite，避免 tft_ 直接绘制中文。
    TFT_eSprite bottomSprite(&tft_);
    bottomSprite.setColorDepth(16);

    if (bottomSprite.createSprite(320, 35)) {
      bottomSprite.fillSprite(TFT_BLACK);

      FontGB2312::drawCenterString(bottomSprite, 160, 5,
                                   "自动扫描并连接JK保护板",
                                   TFT_LIGHTGREY, TFT_BLACK, 1);

      bottomSprite.pushSprite(0, 130);
      bottomSprite.deleteSprite();
    }
  }

  // 只刷新进度条内部区域，不碰其它区域。
  TFT_eSprite scanSprite(&tft_);
  scanSprite.setColorDepth(16);
  scanSprite.createSprite(244, 12);
  scanSprite.fillSprite(TFT_BLACK);

  int progress = (d.scanAttempt * 100) / 3;
  if (progress > 100) progress = 100;
  if (progress < 0) progress = 0;

  int filled = 238 * progress / 100;
  if (filled > 0)
    scanSprite.fillRoundRect(1, 1, filled, 10, 4, TFT_BLUE);

  scanSprite.pushSprite(38, 75);
  scanSprite.deleteSprite();

  // 次数单独做一个很小的局部 Sprite。
  TFT_eSprite countSprite(&tft_);
  countSprite.setColorDepth(16);
  countSprite.createSprite(80, 25);
  countSprite.fillSprite(TFT_BLACK);
  FontGB2312::drawCenterString(countSprite, 40, 2,
                               String(d.scanAttempt) + "/3",
                               TFT_YELLOW, TFT_BLACK, 1);
  countSprite.pushSprite(120, 96);
  countSprite.deleteSprite();
}

void Display::drawFullPage(const BmsData& d) {
  tft_.fillScreen(TFT_BLACK);
  firstDashboard_ = (d.online || d.bootState == BOOT_CONNECTED);

  if (d.bootState == BOOT_SCANNING ||
      d.bootState == BOOT_START) {
    TFT_eSprite page(&tft_);
    page.setColorDepth(16);
    page.createSprite(320, 170);
    page.fillSprite(TFT_BLACK);

    FontGB2312::drawCenterString(page, 160, 7,
                                 "连接电池",
                                 TFT_CYAN, TFT_BLACK, 2);
    FontGB2312::drawCenterString(
        page, 160, 43,
        d.statusMessage.length() ? d.statusMessage : "扫描蓝牙...",
        TFT_WHITE, TFT_BLACK, 1);

    int w = 250;
    page.drawRoundRect(35, 72, w, 18, 5, TFT_DARKGREY);

    int progress = (d.scanAttempt * 100) / 3;
    if (progress > 100) progress = 100;

    if (progress > 0) {
      page.fillRoundRect(38, 75,
                         (w - 6) * progress / 100,
                         12, 4, TFT_BLUE);
    }

    FontGB2312::drawCenterString(page, 160, 96,
                                 String(d.scanAttempt) + "/3",
                                 TFT_YELLOW, TFT_BLACK, 1);
    FontGB2312::drawCenterString(page, 160, 135,
                                 "自动扫描并连接JK保护板",
                                 TFT_LIGHTGREY, TFT_BLACK, 1);

    page.pushSprite(0, 0);
    page.deleteSprite();
    return;
  }

  if (d.bootState == BOOT_CONNECTING) {
    // 连接页也禁止 320x170 全屏 Sprite。
    // BLE 连接阶段堆内存最紧张，此时申请约 109KB 连续 RAM 容易失败，
    // 随后进入热点时又要启动 WiFi，可能表现为重启。
    // 这里先直接清黑屏，再使用几个小 Sprite。
    tft_.fillScreen(TFT_BLACK);

    TFT_eSprite title(&tft_);
    title.setColorDepth(16);
    if(title.createSprite(320, 32)){
      title.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(title, 160, 4,
                                   "正在连接",
                                   TFT_YELLOW, TFT_BLACK, 2);
      title.pushSprite(0, 5);
      title.deleteSprite();
    }

    TFT_eSprite mac(&tft_);
    mac.setColorDepth(16);
    if(mac.createSprite(320, 28)){
      mac.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(mac, 160, 4,
                                   d.mac.length() ? d.mac : "JK-BMS",
                                   TFT_WHITE, TFT_BLACK, 1);
      mac.pushSprite(0, 47);
      mac.deleteSprite();
    }

    TFT_eSprite hint(&tft_);
    hint.setColorDepth(16);
    if(hint.createSprite(320, 28)){
      hint.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(hint, 160, 4,
                                   "正在建立蓝牙连接...",
                                   TFT_WHITE, TFT_BLACK, 1);
      hint.pushSprite(0, 82);
      hint.deleteSprite();
    }
    return;
  }

  if ((d.bootState == BOOT_HOTSPOT || d.hotspot) &&
      !d.online) {
    // 热点页禁止申请 320x170 的全屏 Sprite。
    // 320x170x16bit 约需要 109KB 连续 RAM；连续 BLE 扫描三次后，
    // 堆内存可能已经碎片化，导致 createSprite() 失败，表现为
    // “背光亮但屏幕没有界面”。
    // 改为：背景直接清屏，中文/英文分别使用小尺寸局部 Sprite。
    tft_.fillScreen(TFT_BLACK);

    TFT_eSprite title(&tft_);
    title.setColorDepth(16);
    if(title.createSprite(320, 32)){
      title.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(title, 160, 3,
                                   "热点设置",
                                   TFT_YELLOW, TFT_BLACK, 2);
      title.pushSprite(0, 3);
      title.deleteSprite();
    }

    TFT_eSprite wifi(&tft_);
    wifi.setColorDepth(16);
    if(wifi.createSprite(320, 28)){
      wifi.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(wifi, 160, 3,
                                   "WiFi: JK-BMS-SETUP",
                                   TFT_WHITE, TFT_BLACK, 1);
      wifi.pushSprite(0, 40);
      wifi.deleteSprite();
    }

    TFT_eSprite hint(&tft_);
    hint.setColorDepth(16);
    if(hint.createSprite(320, 28)){
      hint.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(hint, 160, 3,
                                   "手机连接后打开网页",
                                   TFT_CYAN, TFT_BLACK, 1);
      hint.pushSprite(0, 69);
      hint.deleteSprite();
    }

    TFT_eSprite ip(&tft_);
    ip.setColorDepth(16);
    if(ip.createSprite(320, 30)){
      ip.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(
          ip, 160, 3,
          d.hotspotIp.length() ? d.hotspotIp : "192.168.4.1",
          TFT_CYAN, TFT_BLACK, 2);
      ip.pushSprite(0, 98);
      ip.deleteSprite();
    }

    TFT_eSprite bottom(&tft_);
    bottom.setColorDepth(16);
    if(bottom.createSprite(320, 28)){
      bottom.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(bottom, 160, 3,
                                   "扫描 / 选择 / 连接电池",
                                   TFT_LIGHTGREY, TFT_BLACK, 1);
      bottom.pushSprite(0, 133);
      bottom.deleteSprite();
    }
    return;
  }

  drawDashboard(d, true);
}

void Display::drawDashboard(const BmsData& d, bool force) {
  if (force) {
    tft_.fillScreen(TFT_BLACK);

    drawSoc(d);
    drawVoltage(d);
    drawCurrent(d);
    drawPower(d);
    drawTemperature(d);
    drawRange(d);
    drawSocBar(d);
    return;
  }

  if (changed(d.soc, lastData_.soc, 0.5f)) {
    drawSoc(d);
    drawSocBar(d);
  }

  if (changed(d.totalVoltage, lastData_.totalVoltage, 0.1f)) {
    drawVoltage(d);
  }

  if (changed(d.current, lastData_.current, 0.1f)) {
    drawCurrent(d);
  }

  if (changed(d.power, lastData_.power, 1.0f)) {
    drawPower(d);
  }

  if (changed(d.remainingCapacityAh, lastData_.remainingCapacityAh, 0.1f) ||
      changed(d.temperature1, lastData_.temperature1, 0.1f) ||
      changed(d.deltaCellVoltage, lastData_.deltaCellVoltage, 0.001f)) {
    drawTemperature(d);
  }

  if (changed(d.remainingRangeKm, lastData_.remainingRangeKm, 0.1f)) {
    drawRange(d);
  }
}

void Display::drawSoc(const BmsData& d) {
  socSprite_.fillSprite(TFT_BLACK);
  drawPanel(socSprite_, 0, 0, 140, 78);

  String value = String(d.soc, 0);
  uint16_t color = socColor(d.soc);

  socSprite_.setTextColor(color, UI_PANEL);
  socSprite_.drawCentreString(value, 70, -1, 7);

  socSprite_.setTextColor(color, UI_PANEL);
  socSprite_.drawString("%", 108, 47, 4);

  socSprite_.pushSprite(4, 4);
}

void Display::drawTemperature(const BmsData& d) {
  leftInfoSprite_.fillSprite(TFT_BLACK);
  leftInfoSprite_.fillRoundRect(0, 0, 73, 67, 7, UI_PANEL);

  String temp = String(d.temperature1, 1) + "C";
  String delta = String(d.deltaCellVoltage * 1000.0f, 0) + "mV";

  leftInfoSprite_.setTextColor(UI_TEMP, UI_PANEL);
  leftInfoSprite_.drawString(temp, 4, 3, 2);
  leftInfoSprite_.drawString(delta, 4, 31, 2);

  leftInfoSprite_.fillRoundRect(75, 0, 65, 67, 7, UI_PANEL);

  leftInfoSprite_.setTextColor(UI_WHITE, UI_PANEL);
  leftInfoSprite_.drawCentreString("Ah", 107, 1, 2);

  String cap = String(d.remainingCapacityAh, 1);
  leftInfoSprite_.drawCentreString(cap, 107, 24, 4);

  leftInfoSprite_.pushSprite(4, 84);
}

void Display::drawVoltage(const BmsData& d) {
  drawMetricRow(rowSprite_, 'V', "电压",
                String(d.totalVoltage, 2) + "V",
                UI_VOLTAGE);
  rowSprite_.pushSprite(148, 4);
}

void Display::drawCurrent(const BmsData& d) {
  drawMetricRow(rowSprite_, 'A', "电流",
                String(d.current, 1) + "A",
                UI_CURRENT);
  rowSprite_.pushSprite(148, 33);
}

void Display::drawPower(const BmsData& d) {
  drawMetricRow(rowSprite_, 'W', "功率",
                String(d.power, 0) + "W",
                UI_POWER);
  rowSprite_.pushSprite(148, 62);
}

void Display::drawRange(const BmsData& d) {
  rowSprite_.fillSprite(TFT_BLACK);
  rowSprite_.fillRoundRect(0, 0, 168, 27, 7, UI_PANEL);

  FontGB2312::drawText(rowSprite_, 8, 5,
                       "剩余里程",
                       UI_RANGE, UI_PANEL, 1);

  rowSprite_.setTextColor(UI_RANGE, UI_PANEL);
  rowSprite_.drawRightString(
      String(d.remainingRangeKm, 0) + " KM",
      164, 5, 2);

  rowSprite_.pushSprite(148, 91);
}

void Display::drawSocBar(const BmsData& d) {
  barSprite_.fillSprite(TFT_BLACK);

  float ratio = d.soc / 100.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  const int16_t w = 312;
  const int16_t h = 11;
  int filled = (int)(w * ratio + 0.5f);

  for (int16_t x = 0; x < w; ++x) {
    if (x >= filled) {
      barSprite_.drawFastVLine(x, 0, h, UI_PANEL_DARK);
      continue;
    }

    uint16_t color;

    if (w <= 1) {
      color = TFT_GREEN;
    } else {
      uint16_t p = (uint16_t)((x * 100L) / (w - 1));

      if (p <= 50) {
        color = lerp565(TFT_GREEN, TFT_YELLOW,
                        (uint16_t)(p * 2));
      } else {
        color = lerp565(TFT_YELLOW, TFT_RED,
                        (uint16_t)((p - 50) * 2));
      }
    }

    barSprite_.drawFastVLine(x, 0, h, color);
  }

  barSprite_.drawRoundRect(0, 0, w, h, 3, TFT_DARKGREY);
  barSprite_.pushSprite(4, 157);
}


// ===== src/web/WebConfig.cpp =====
WebConfig::WebConfig():server_(80),ble_(nullptr),active_(false){}

String WebConfig::jsonEscape(const String& s){
  String o;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(c=='"') o+="\\\"";
    else if(c=='\\') o+="\\\\";
    else if(c=='\n') o+="\\n";
    else if(c=='\r') o+="\\r";
    else if(c=='\t') o+="\\t";
    else o+=c;
  }
  return o;
}

String WebConfig::makeStatusJson(){
  String j="{";
  j+="\"online\":"+String(g_bmsData.online?"true":"false");
  j+=",\"state\":"+String((int)g_bmsData.bootState);
  j+=",\"message\":\""+jsonEscape(g_bmsData.statusMessage)+"\"";
  j+=",\"mac\":\""+jsonEscape(g_bmsData.mac.length()?g_bmsData.mac:ble_?ble_->getConfiguredAddress():"")+"\"";
  j+=",\"name\":\""+jsonEscape(g_bmsData.deviceName)+"\"";
  j+=",\"proto\":"+String(ble_ && ble_->isProtocol32S()?"32":"24");
  j+=",\"ip\":\""+jsonEscape(WiFi.softAPIP().toString())+"\"";
  j+=",\"scanCount\":"+String(ble_?ble_->getScanCount():0);
  j+=",\"scanAttempt\":"+String(g_bmsData.scanAttempt);
  j+=",\"voltage\":"+String(g_bmsData.totalVoltage,3);
  j+=",\"current\":"+String(g_bmsData.current,3);
  j+=",\"power\":"+String(g_bmsData.power,1);
  j+=",\"remainingAh\":"+String(g_bmsData.remainingCapacityAh,2);
  j+=",\"remainingKm\":"+String(g_bmsData.remainingRangeKm,1);
  j+=",\"consumption\":"+String(g_bmsData.energyConsumptionWhKm,1);
  j+=",\"totalAh\":"+String(g_bmsData.totalCapacityAh,2);
  j+=",\"soc\":"+String(g_bmsData.soc,1);
  return j+"}";
}

String WebConfig::makePage(){
  return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>JK BMS 设置</title>
<style>
*{box-sizing:border-box}
body{font-family:Arial,"Microsoft YaHei",sans-serif;background:#0b1015;color:#eee;margin:0;padding:14px}
.card{max-width:760px;margin:auto;background:#151c23;border-radius:16px;padding:18px;box-shadow:0 5px 25px #000}
h2{margin:0 0 12px}
button{padding:11px 15px;margin:5px;border:0;border-radius:9px;background:#1976d2;color:#fff;font-size:15px}
input,select{padding:11px;margin:5px 0;width:100%;border-radius:8px;border:1px solid #4a5662;background:#0d1217;color:#fff}
.item{padding:12px;border:1px solid #394652;border-radius:10px;margin:8px 0;background:#10161c}
.row{display:flex;gap:8px;align-items:center;justify-content:space-between}
.small{color:#aeb8c2;font-size:13px}
.ok{color:#45e27b}.warn{color:#ffc107}
.data{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px}
.data div{background:#0e141a;border-radius:9px;padding:10px}
.num{font-size:20px;font-weight:bold;color:#55d9ff}
.tip{color:#8e9aa5;font-size:13px;margin:4px 0 10px}
</style>
</head>
<body>
<div class="card">
<h2>JK BMS 蓝牙设置</h2>
<div id="status" class="small">读取状态...</div>

<label>保护板 MAC（可留空自动扫描）</label>
<input id="mac" placeholder="例如 AA:BB:CC:DD:EE:FF">

<label>协议型号</label>
<select id="proto">
<option value="32">JK02_32S</option>
<option value="24">JK02_24S</option>
</select>

<label>每公里耗电量（Wh/km）</label>
<input id="consumption" type="number" min="1" max="1000" step="1" value="100">
<div class="tip">用于估算剩余公里数：剩余容量 × 电压 ÷ 每公里耗电量</div>

<div>
<button onclick="scan()">扫描蓝牙电池</button>
<button onclick="save()">保存参数</button>
</div>
<div id="list"></div>

<div class="data">
<div>电压<br><span id="v" class="num">0 V</span></div>
<div>电流<br><span id="i" class="num">0 A</span></div>
<div>功率<br><span id="p" class="num">0 W</span></div>
<div>剩余容量<br><span id="ah" class="num">0 Ah</span></div>
<div>剩余公里数<br><span id="km" class="num">0 km</span></div>
<div>SOC<br><span id="soc" class="num">0%</span></div>
</div>
</div>

<script>
async function api(url,opt){return await (await fetch(url,opt)).json();}
async function status(){
  try{
    let s=await api('/api/status');
    document.getElementById('status').innerHTML=
      '<b class="'+(s.online?'ok':'warn')+'">'+(s.online?'已连接':'未连接')+
      '</b>　'+s.message+'<br>MAC: '+(s.mac||'未设置')+'　IP: '+s.ip;
    // 连接成功后，把实际连接的蓝牙地址自动回填到 MAC 输入框。
    if(s.mac) document.getElementById('mac').value=s.mac;
    if(s.proto) document.getElementById('proto').value=s.proto;
    document.getElementById('v').textContent=s.voltage.toFixed(2)+' V';
    document.getElementById('i').textContent=s.current.toFixed(2)+' A';
    document.getElementById('p').textContent=s.power.toFixed(0)+' W';
    document.getElementById('ah').textContent=s.remainingAh.toFixed(2)+' Ah';
    document.getElementById('km').textContent=s.remainingKm.toFixed(1)+' km';
    document.getElementById('soc').textContent=s.soc.toFixed(0)+'%';
    document.getElementById('consumption').value=s.consumption.toFixed(0);
  }catch(e){}
}
async function scan(){
  document.getElementById('list').innerHTML='正在扫描蓝牙电池，请等待 5 秒...';
  let r=await api('/api/scan');
  let h='<h3>扫描结果（点击连接）</h3>';
  if(!r.items.length) h+='<div class="item">没有找到 JK / BMS 设备</div>';
  r.items.forEach((x,i)=>{
    h+='<div class="item"><div class="row"><b>'+x.name+'</b><span>RSSI '+x.rssi+' dBm</span></div>'+
       '<div class="small">'+x.address+'</div>'+
       '<button onclick="connectTo('+i+')">连接此电池</button></div>';
  });
  document.getElementById('list').innerHTML=h;
}
async function connectTo(i){
  document.getElementById('status').textContent='正在连接选中的蓝牙电池...';
  let r=await api('/api/connect?index='+i);
  document.getElementById('status').textContent=r.message;
  if(r.mac) document.getElementById('mac').value=r.mac;
  status();
}
async function save(){
  let fd=new FormData();
  fd.append('mac',document.getElementById('mac').value);
  fd.append('proto',document.getElementById('proto').value);
  fd.append('consumption',document.getElementById('consumption').value);
  let r=await api('/api/save',{method:'POST',body:fd});
  alert(r.message);
  status();
}
status();
setInterval(status,1000);
</script>
</body></html>)HTML";
}

void WebConfig::begin(BmsBle* ble){
  ble_=ble;
  active_=true;

  // 从 Preferences 读取上次保存的单位里程耗电量。
  Preferences p;
  p.begin("jkcfg",true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm",100.0f);
  p.end();

  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;

  server_.on("/",HTTP_GET,[this](){handleRoot();});
  server_.on("/api/status",HTTP_GET,[this](){handleStatus();});
  server_.on("/api/scan",HTTP_GET,[this](){handleScan();});
  server_.on("/api/connect",HTTP_GET,[this](){handleConnect();});
  server_.on("/api/save",HTTP_POST,[this](){handleSave();});
  server_.onNotFound([this](){handleNotFound();});
  server_.begin();
}

void WebConfig::loop(){
  if(active_) server_.handleClient();
}

void WebConfig::handleRoot(){
  server_.send(200,"text/html; charset=utf-8",makePage());
}

void WebConfig::handleStatus(){
  server_.send(200,"application/json; charset=utf-8",makeStatusJson());
}

void WebConfig::handleScan(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\",\"items\":[]}");
    return;
  }
  uint8_t count=ble_->scanDevices(5);
  String j="{\"message\":\"扫描完成\",\"items\":[";
  for(uint8_t i=0;i<count;i++){
    if(i) j+=",";
    const BmsScanItem& x=ble_->getScanItem(i);
    j+="{\"name\":\""+jsonEscape(x.name)+"\",\"address\":\""+
      jsonEscape(x.address)+"\",\"rssi\":"+String(x.rssi)+"}";
  }
  j+="]}";
  server_.send(200,"application/json; charset=utf-8",j);
}

void WebConfig::handleConnect(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }
  if(!server_.hasArg("index")){
    server_.send(400,"application/json","{\"message\":\"缺少index\"}");
    return;
  }
  String indexText=server_.arg("index");
  indexText.trim();
  if(indexText.length()==0){
    server_.send(400,"application/json; charset=utf-8","{\"message\":\"index无效\"}");
    return;
  }

  int index=indexText.toInt();
  if(index<0 || index>=ble_->getScanCount()){
    server_.send(400,"application/json; charset=utf-8","{\"message\":\"index超出扫描结果范围\"}");
    return;
  }

  bool ok=ble_->connectDeviceByIndex((uint8_t)index);
  String j="{\"ok\":" + String(ok?"true":"false")+
           ",\"message\":\""+String(ok?"连接成功":"连接失败")+"\""+
           ",\"mac\":\""+jsonEscape(g_bmsData.mac)+"\"}";
  server_.send(ok?200:500,"application/json; charset=utf-8",j);
}

void WebConfig::handleSave(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }

  String mac=server_.hasArg("mac")?server_.arg("mac"):"";
  mac.trim();

  // 没有手工填写时，直接保存当前已经连接的 JK 蓝牙地址。
  if(mac.length()==0 && g_bmsData.mac.length())
    mac=g_bmsData.mac;

  bool is32=server_.hasArg("proto") ? server_.arg("proto")=="32" : true;

  float whkm=server_.hasArg("consumption") ? server_.arg("consumption").toFloat() : 100.0f;
  if(whkm<1.0f) whkm=1.0f;
  if(whkm>1000.0f) whkm=1000.0f;

  ble_->setConfiguredAddress(mac);
  ble_->setProtocol32S(is32);

  g_bmsData.energyConsumptionWhKm=whkm;

  Preferences p;
  p.begin("jkcfg",false);
  p.putBool("32s",is32);
  p.putFloat("whkm",whkm);
  p.end();

  // 保存后立即重新计算一次，网页不用重启即可看到新结果。
  if(g_bmsData.totalVoltage>0.1f && whkm>1.0f){
    g_bmsData.remainingRangeKm=
      (g_bmsData.remainingCapacityAh*g_bmsData.totalVoltage)/whkm;
  }

  server_.send(200,"application/json; charset=utf-8",
               "{\"message\":\"参数已保存，下次开机自动使用\""
               ",\"mac\":\""+jsonEscape(mac)+"\"}");
}

void WebConfig::handleNotFound(){
  server_.send(404,"text/plain; charset=utf-8","404");
}


// ===== src/main.cpp =====
static BmsBle bmsBle;
static Display display;
static WebConfig webConfig;
static const char* AP_SSID="JK-BMS-SETUP";
static const char* AP_PASSWORD="12345678";

RTC_DATA_ATTR static uint8_t rtcFailedScanAttempts=0;

static void startHotspot(){
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
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.hotspot=false;

  // 初始化显示和 BLE。
  display.begin();
  bmsBle.begin();

  // ================================
  // 开机优先检查“已经保存的蓝牙地址”
  // ================================
  const String savedMac=bmsBle.getConfiguredAddress();

  if(savedMac.length()==0){
    // 没有保存过蓝牙，直接进入热点配网。
    g_bmsData.scanAttempt=0;
    g_bmsData.bootState=BOOT_HOTSPOT;
    g_bmsData.statusMessage="未保存蓝牙，进入配网";
    display.update(g_bmsData);
    delay(100);
    startHotspot();
    display.update(g_bmsData);
    return;
  }

  // 有保存地址：只尝试连接这个地址，不再盲目扫描其它设备。
  g_bmsData.scanAttempt=1;
  g_bmsData.bootState=BOOT_CONNECTING;
  g_bmsData.statusMessage="连接已保存蓝牙";
  g_bmsData.mac=savedMac;
  display.update(g_bmsData);
  delay(100);

  Serial.printf("BOOT: saved JK MAC = %s\\n",savedMac.c_str());

  bool connectedOk=bmsBle.connectByAddress(savedMac);

  // GATT 连接建立后，还必须收到有效 JK 数据，才算真正成功。
  if(connectedOk && bmsBle.connected()){
    uint32_t verifyStart=millis();
    while(bmsBle.connected() && !g_bmsData.valid &&
          millis()-verifyStart<4000UL){
      bmsBle.loop();
      delay(20);
    }
    connectedOk=bmsBle.connected() && g_bmsData.valid;
  }

  if(connectedOk){
    g_bmsData.bootState=BOOT_CONNECTED;
    g_bmsData.online=true;
    g_bmsData.statusMessage="已连接保存的JK电池";
    display.update(g_bmsData);
    Serial.println("BOOT: saved Bluetooth connected and JK data valid.");
    return;
  }

  // 保存地址连接失败：先彻底释放 BLE Client，再进入热点。
  // 特别是“GATT 已连接但 4 秒内没有有效 JK 数据”的情况，
  // 此时 client_ 仍可能保持连接；不释放就直接启动 AP，容易触发 C3 重启。
  bmsBle.releaseConnectionForHotspot();
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="蓝牙连接失败，进入配网";
  display.update(g_bmsData);
  delay(100);

  Serial.println("BOOT: saved Bluetooth connection failed, entering hotspot.");
  startHotspot();
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
