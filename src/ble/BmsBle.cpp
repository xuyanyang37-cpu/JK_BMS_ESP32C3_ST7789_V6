/*
 * ================================================================
 * BmsBle.cpp - 蓝牙业务层
 *
 * 业务顺序：
 *   begin()
 *      ↓
 *   读取 Preferences
 *      ↓
 *   scanAndConnect()
 *      ↓
 *   scanDevices()
 *      ↓
 *   connectByAddress()
 *      ↓
 *   找服务/特征 + subscribe
 *      ↓
 *   request()
 *      ↓
 *   Notify
 *      ↓
 *   handleNotification()
 *      ↓
 *   BmsProtocolManager
 *      ↓
 *   g_bmsData
 *
 * 这里“只负责传输”，协议字段含义由 protocol/ 负责。
 * ================================================================
 */

#include "BmsBle.h"
#include <Preferences.h>
#include <string.h>

static const char* SERVICE="FFE0";
static const char* JK_NOTIFY_UUID="FFE1";
static const char* JK_WRITE_UUID="FFE2";
BmsBle* BmsBle::instance_=nullptr;
static uint8_t g_rxBuf[700];
static size_t g_rxLen=0;
static bool g_legacyAckSeen=false;

BmsBle::BmsBle()
  : client_(nullptr),ch_(nullptr),writeCh_(nullptr),notifyCh_(nullptr),
    counter_(0),lastRequest_(0),lastReconnectAttempt_(0),recoveryVerifyStart_(0),
    recoveryFailures_(0),runtimeRecoveryEnabled_(false),
    scanCount_(0),scanAttempt_(0),configuredAddressType_(BLE_ADDR_PUBLIC),
    protocol32S_(true),configuredAddress_("") {
  instance_=this;
}

// [入口] BLE初始化。开机只调用一次。\nbool BmsBle::begin(){
  NimBLEDevice::init("JK-C3-DISPLAY");
  NimBLEDevice::setPower(9);

  Preferences p;
  p.begin("jkcfg", true);
  configuredAddress_=p.getString("mac", "");
  configuredAddressType_=p.getUChar("mactype", BLE_ADDR_PUBLIC);
  protocol32S_=p.getBool("32s", true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm", 100.0f);
  String preferredProtocol=p.getString("protocol", "JK");
  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;
  p.end();

  protocolManager_.begin(protocol32S_);
  if(preferredProtocol.length()) protocolManager_.setPreferredProtocol(preferredProtocol);

  // 预留固定容量，避免三轮扫描时反复扩容 String，
  // 减少 ESP32-C3 堆碎片，为最后启动 SoftAP 留出连续内存。
  for(uint8_t i=0;i<BMS_SCAN_RESULT_MAX;i++){
    scanItems_[i].address.reserve(18);
    scanItems_[i].name.reserve(24);
  }

  return true;
}

void BmsBle::setPreferredProtocol(const String& name){
  if(protocolManager_.setPreferredProtocol(name)){
    Preferences p;
    p.begin("jkcfg",false);
    p.putString("protocol",protocolManager_.preferredProtocolName());
    p.end();
  }
}

String BmsBle::getPreferredProtocol() const {
  return String(protocolManager_.preferredProtocolName());
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
         n.indexOf("JK")>=0 || n.indexOf("JIKONG")>=0 || n.indexOf("BMS")>=0 ||
         n.indexOf("ANT")>=0 || n.indexOf("JBD")>=0 || n.indexOf("DALY")>=0 ||
         n.indexOf("TT")>=0 || n.indexOf("铁塔")>=0;
}

// [流程1] 扫描附近设备，只保留 JK/BMS/指定Service 的候选设备。\nuint8_t BmsBle::scanDevices(uint32_t sec){
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

// [流程2] 一次完整的“扫描 -> 选设备 -> 连接”业务。main.cpp负责最多调用3轮。\nbool BmsBle::scanAndConnect(uint32_t sec, uint8_t attemptOverride){
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

  // ================================================================
  // 快速路径：已经保存MAC时，先直接连接保存设备。
  // 这样正常使用时不必每次上电先完整扫描3秒。
  //
  // 失败后再进入扫描路径：
  //   保存MAC直连失败
  //        ↓
  //   扫描附近候选设备
  //        ↓
  //   选择RSSI最强设备
  // ================================================================
  if(configuredAddress_.length()){
    setStatus(BOOT_CONNECTING,"连接已保存BMS");
    Serial.printf("BLE FAST CONNECT: %s type=%u\\n",
                  configuredAddress_.c_str(),configuredAddressType_);
    if(connectByAddress(configuredAddress_,configuredAddressType_))
      return true;

    Serial.println("BLE FAST CONNECT: saved device failed, fallback to scan.");
  }

  setStatus(BOOT_SCANNING,"扫描蓝牙电池 "+String(scanAttempt_)+"/3");
  scanDevices(sec);

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

// [流程3] 按MAC连接，并完成 GATT 服务、读写特征、通知订阅。\nbool BmsBle::connectByAddress(const String& address,uint8_t addressType){
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

  // 标准角色优先：FFE1 通知、FFE2 写入；若固件交换属性，再按 capability 自动寻找。
  NimBLERemoteCharacteristic* ffe1=s->getCharacteristic(NimBLEUUID(JK_NOTIFY_UUID));
  NimBLERemoteCharacteristic* ffe2=s->getCharacteristic(NimBLEUUID(JK_WRITE_UUID));

  writeCh_=nullptr;
  notifyCh_=nullptr;
  if(ffe2 && (ffe2->canWriteNoResponse() || ffe2->canWrite())) writeCh_=ffe2;
  if(ffe1 && (ffe1->canNotify() || ffe1->canIndicate())) notifyCh_=ffe1;

  NimBLERemoteCharacteristic* chars[2]={ffe1,ffe2};
  for(int i=0;i<2;i++){
    NimBLERemoteCharacteristic* c=chars[i];
    if(!c) continue;
    if(!writeCh_ && (c->canWriteNoResponse() || c->canWrite())) writeCh_=c;
    if(!notifyCh_ && (c->canNotify() || c->canIndicate())) notifyCh_=c;
  }

  Serial.printf("JK BLE chars: FFE1 W=%d N=%d; FFE2 W=%d N=%d\n",
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
  g_rxLen=0;
  g_legacyAckSeen=false;
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

  // 根据“已保存协议”选择首次请求。
  // JK/ANT/TT 已经保存时，不再混发其它协议命令，减少误探测和无效流量。
  String proto=String(protocolManager_.protocolName());
  if(proto=="ANT"){
    requestAntStatus();
  } else if(proto=="TT"){
    request(0x03);
  } else if(proto=="JBD" || proto=="DALY"){
    // 当前项目对应协议的 buildCommand() 决定是否支持实际查询。
    // 不支持时不会发送伪造的 JK/ANT/TT 指令。
    request(0x03);
  } else {
    // 默认/未锁定协议仍使用兼容探测：JK -> ANT -> TT。
    request(0x96);
    delay(100);
    request(0x97);
    delay(100);
    requestAntStatus();
    delay(100);
    requestTtProbe();
  }
  lastRequest_=millis();
  return true;
}

void BmsBle::notifyCallback(NimBLERemoteCharacteristic*,uint8_t* d,size_t n,bool){
  if(instance_) instance_->handleNotification(d,n);
}

// [流程4] BLE通知入口：只负责组帧/拆帧，不解析协议字段。
// 兼容：半帧、粘包、噪声、FC xx 06 短ACK；完整帧再交给 BmsProtocolManager。
void BmsBle::handleNotification(const uint8_t* d,size_t n){
  if(!d || n==0) return;
  Serial.printf("BMS RX notify len=%u: ",(unsigned)n);
  size_t dump=n<24?n:24;
  for(size_t i=0;i<dump;i++) Serial.printf("%02X ",d[i]);
  if(n>dump) Serial.print("...");
  Serial.println();

  if(n>sizeof(g_rxBuf) || g_rxLen+n>sizeof(g_rxBuf)){
    Serial.println("BMS RX: buffer overflow, reset");
    g_rxLen=0;
    return;
  }
  memcpy(g_rxBuf+g_rxLen,d,n);
  g_rxLen+=n;

  while(g_rxLen>=3){
    bool ackFound=false;
    for(size_t i=0;i+2<g_rxLen;){
      if(g_rxBuf[i]==0xFC && g_rxBuf[i+2]==0x06){
        Serial.printf("BMS RX: legacy ACK FC %02X 06\n",g_rxBuf[i+1]);
        g_legacyAckSeen=true;
        ackFound=true;
        memmove(g_rxBuf+i,g_rxBuf+i+3,g_rxLen-(i+3));
        g_rxLen-=3;
        continue;
      }
      ++i;
    }

    if(g_rxLen<4){
      if(ackFound) lastRequest_=0;
      break;
    }

    int startIdx=protocolManager_.findFrameStart(g_rxBuf,g_rxLen);
    if(startIdx<0){
      if(g_rxLen>3){
        memmove(g_rxBuf,g_rxBuf+g_rxLen-3,3);
        g_rxLen=3;
      }
      break;
    }

    if(startIdx>0){
      memmove(g_rxBuf,g_rxBuf+startIdx,g_rxLen-startIdx);
      g_rxLen-=startIdx;
      if(g_rxLen<4) break;
    }

    size_t expected=protocolManager_.frameLength(g_rxBuf,g_rxLen);
    if(expected==0) break;
    if(expected>sizeof(g_rxBuf)){
      Serial.printf("BMS RX: invalid frame length=%u, reset\n",(unsigned)expected);
      g_rxLen=0;
      break;
    }
    if(g_rxLen<expected) break;

    if(protocolManager_.parseFrame(g_rxBuf,expected,g_bmsData)){
      g_bmsData.online=true;
      g_bmsData.updateMs=millis();
      lastRequest_=millis();

      // 自动识别成功后，把协议写入 Preferences。
      // 只有协议真正发生变化时才写 Flash，避免每一帧都产生擦写。
      String detected=String(protocolManager_.protocolName());
      Preferences p;
      p.begin("jkcfg",false);
      String saved=p.getString("protocol","");
      if(detected.length() && detected!="NONE" && detected!=saved){
        p.putString("protocol",detected);
        Serial.printf("BMS protocol locked: %s\n",detected.c_str());
      }
      p.end();
    }

    memmove(g_rxBuf,g_rxBuf+expected,g_rxLen-expected);
    g_rxLen-=expected;
  }
  if(g_legacyAckSeen && g_rxLen==0){ lastRequest_=0; g_legacyAckSeen=false; }
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

// [运行期] 连接保持、断线恢复、定时请求。
// 规则：任何运行期 BLE 恢复连续失败3次，都进入热点配置。
void BmsBle::loop(){
  uint32_t now=millis();

  if(!connected()){
    g_bmsData.online=false;
    recoveryVerifyStart_=0;

    // 只有首次启动已经成功进入正常运行后，才启用“断线恢复3次失败 -> 热点”。
    if(runtimeRecoveryEnabled_ && g_bmsData.bootState!=BOOT_HOTSPOT &&
       now-lastReconnectAttempt_>=5000UL){
      lastReconnectAttempt_=now;
      uint8_t reconnectAttempt=scanAttempt_;
      if(reconnectAttempt<1 || reconnectAttempt>3) reconnectAttempt=1;

      Serial.printf("BLE RECOVERY: attempt %u/3, previous failures=%u\\n",
                    reconnectAttempt,recoveryFailures_);

      if(scanAndConnect(3,reconnectAttempt)){
        // GATT已经恢复，但必须继续等待真正的BMS有效帧。
        recoveryVerifyStart_=now;
        if(g_bmsData.valid){
          recoveryFailures_=0;
          recoveryVerifyStart_=0;
          Serial.println("BLE RECOVERY: GATT + valid BMS data restored.");
        }
      } else {
        recoveryFailures_++;
        Serial.printf("BLE RECOVERY: failed %u/3\\n",recoveryFailures_);
      }

      if(recoveryFailures_>=3){
        Serial.println("BLE RECOVERY: 3 consecutive failures -> HOTSPOT");
        recoveryFailures_=0;
        recoveryVerifyStart_=0;
        releaseConnectionForHotspot();
        g_bmsData.bootState=BOOT_HOTSPOT;
        g_bmsData.statusMessage="蓝牙恢复连续3次失败，进入配网";
        return;
      }
    }
    return;
  }

  // GATT连接成功但尚未收到有效BMS数据：给协议探测最多4秒。
  // 超时视为一次恢复失败，避免“连接成功但数据死掉”永久卡在在线状态。
  if(runtimeRecoveryEnabled_ && !g_bmsData.valid && recoveryVerifyStart_!=0 &&
     now-recoveryVerifyStart_>=4000UL){
    Serial.println("BLE RECOVERY: connected but no valid BMS frame.");
    if(client_) client_->disconnect();
    g_bmsData.online=false;
    g_bmsData.valid=false;
    g_rxLen=0;
    g_legacyAckSeen=false;
    recoveryVerifyStart_=0;
    recoveryFailures_++;
    lastReconnectAttempt_=now-5000UL;

    if(recoveryFailures_>=3){
      Serial.println("BLE RECOVERY: 3 consecutive failures -> HOTSPOT");
      recoveryFailures_=0;
      releaseConnectionForHotspot();
      g_bmsData.bootState=BOOT_HOTSPOT;
      g_bmsData.statusMessage="蓝牙恢复连续3次失败，进入配网";
    }
    return;
  }

  // 一旦收到有效数据，说明本次恢复真正成功，连续失败计数清零。
  if(runtimeRecoveryEnabled_ && g_bmsData.valid && recoveryVerifyStart_!=0){
    recoveryFailures_=0;
    recoveryVerifyStart_=0;
    Serial.println("BLE RECOVERY: valid BMS data confirmed, failure counter reset.");
  }

  // 已连接但15秒没有任何有效帧，也视为通信恢复失败，重新进入3次恢复状态机。
  if(g_bmsData.valid && g_bmsData.updateMs!=0 && now-g_bmsData.updateMs>15000UL){
    Serial.println("BMS BLE: connected but no valid frame for 15s, reconnect");
    if(client_) client_->disconnect();
    g_bmsData.online=false;
    g_bmsData.valid=false;
    g_rxLen=0;
    g_legacyAckSeen=false;
    recoveryVerifyStart_=0;
    lastReconnectAttempt_=now-5000UL;
    return;
  }

  String proto=String(protocolManager_.protocolName());
  if(proto=="TT"){
    if(now-lastRequest_>=250){ request(0x03); lastRequest_=now; }
  } else if(proto=="ANT"){
    if(now-lastRequest_>=2000){ requestAntStatus(); lastRequest_=now; }
  } else if(proto=="JK"){
    if(now-lastRequest_>=5000){ request(0x96); lastRequest_=now; }
  } else if(proto=="JBD" || proto=="DALY"){
    if(now-lastRequest_>=2000){ request(0x03); lastRequest_=now; }
  }
}

bool BmsBle::connected() const{
  return client_ && client_->isConnected();
}

// [释放] 三次失败进入热点前，必须释放 BLE Client 和扫描资源。\nvoid BmsBle::releaseConnectionForHotspot(){
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
  g_rxLen=0;
  g_legacyAckSeen=false;
  recoveryVerifyStart_=0;

  NimBLEScan* s=NimBLEDevice::getScan();
  if(s){
    s->stop();
    s->clearResults();
  }
}
