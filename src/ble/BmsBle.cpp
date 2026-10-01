#include "BmsBle.h"
#include <Preferences.h>
#include <string.h>

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

  // JK 常见实现中 FFE1/FFE2 的读写方向并不完全一致。
  // 不再把 UUID 强行绑定为 write/notify，而是根据实际 BLE 属性选择。
  NimBLERemoteCharacteristic* ffe1=s->getCharacteristic(NimBLEUUID(WRITE_CHAR));
  NimBLERemoteCharacteristic* ffe2=s->getCharacteristic(NimBLEUUID(NOTIFY_CHAR));

  writeCh_=nullptr;
  notifyCh_=nullptr;

  NimBLERemoteCharacteristic* chars[2]={ffe1,ffe2};
  for(int i=0;i<2;i++){
    NimBLERemoteCharacteristic* c=chars[i];
    if(!c) continue;
    if(!writeCh_ && (c->canWriteNoResponse() || c->canWrite())) writeCh_=c;
    if(!notifyCh_ && (c->canNotify() || c->canIndicate())) notifyCh_=c;
  }

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

    // JK 4E57 是变长帧：长度字段位于 byte[2..3]。
    // 旧 JK 55AAEB90 / 其他固定协议继续按固定长度处理。
    size_t expected=protocolManager_.frameLength(rx,len);
    if(expected==0){
      // 已经找到帧头，但长度字段还不完整或异常，等待下一次 Notify。
      break;
    }

    if(expected>sizeof(rx)){
      Serial.printf("BMS RX: frame too large (%u), reset\\n",(unsigned)expected);
      len=0;
      break;
    }

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
