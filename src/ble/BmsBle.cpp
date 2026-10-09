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

#include <Arduino.h>
#include <HardwareSerial.h>
#include "BmsBle.h"
#include <Preferences.h>
#include <string.h>

static const char* SERVICE="FFE0";
static const char* JK_NOTIFY_UUID="FFE1";
static const char* JK_WRITE_UUID="FFE2";
BmsBle* BmsBle::instance_=nullptr;
static BmsBle* g_scanOwner=nullptr;
static uint8_t g_rxBuf[700];
static size_t g_rxLen=0;
static bool g_legacyAckSeen=false;

BmsBle::BmsBle()
  : client_(nullptr),ch_(nullptr),writeCh_(nullptr),notifyCh_(nullptr),
    yanyangDecoder_(onYanyangStatus,onYanyangDeviceInfo),yanyangMode_(false),
    counter_(0),lastRequest_(0),lastReconnectAttempt_(0),recoveryVerifyStart_(0),
    recoveryFailures_(0),runtimeRecoveryEnabled_(false),
    scanCount_(0),scanAttempt_(0),configuredAddressType_(BLE_ADDR_PUBLIC),
    protocol32S_(true),configuredAddress_("") {
  instance_=this;
  g_scanOwner=this;
}

// [入口] BLE初始化。开机只调用一次。
bool BmsBle::begin(){
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
  if(preferredProtocol.length()) setPreferredProtocol(preferredProtocol);

  return true;
}

void BmsBle::setPreferredProtocol(const String& name){
  String normalized=name;
  normalized.trim();
  normalized.toUpperCase();
  if(normalized=="YANYANG" || normalized=="YY" || normalized=="彦阳"){
    yanyangMode_=true;
    yanyangDecoder_.reset();
    Preferences p;
    p.begin("jkcfg",false);
    p.putString("protocol","YANYANG");
    p.end();
    return;
  }
  yanyangMode_=false;
  if(protocolManager_.setPreferredProtocol(normalized)){
    Preferences p;
    p.begin("jkcfg",false);
    p.putString("protocol",protocolManager_.preferredProtocolName());
    p.end();
  }
}

String BmsBle::getPreferredProtocol() const {
  return String(protocolName());
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
  if(!d) return false;
  std::string n=d->getName();
  for(size_t i=0;i<n.size();++i){
    if(n[i]>='a' && n[i]<='z') n[i]=static_cast<char>(n[i]-'a'+'A');
  }
  return d->isAdvertisingService(NimBLEUUID(SERVICE)) ||
         n.find("JK")!=std::string::npos || n.find("JIKONG")!=std::string::npos ||
         n.find("BMS")!=std::string::npos || n.find("ANT")!=std::string::npos ||
         n.find("JBD")!=std::string::npos || n.find("DALY")!=std::string::npos ||
         n.find("TT")!=std::string::npos || n.find("铁塔")!=std::string::npos ||
         n.find("YANYANG")!=std::string::npos || n.find("YY")!=std::string::npos || n.find("彦阳")!=std::string::npos;
}

void BmsBle::onScanResult(const NimBLEAdvertisedDevice* d){
  if(!isCandidate(d)) return;
  const int rssi=d->getRSSI();
  if(scanCount_!=0 && rssi<=scanItems_[0].rssi) return;
  const std::string address=d->getAddress().toString();
  const std::string name=d->haveName() ? d->getName() : std::string("JK-BMS");
  snprintf(scanItems_[0].address,sizeof(scanItems_[0].address),"%s",address.c_str());
  snprintf(scanItems_[0].name,sizeof(scanItems_[0].name),"%s",name.c_str());
  scanItems_[0].addressType=d->getAddressType();
  scanItems_[0].rssi=rssi;
  scanCount_=1;
}

// [流程1] 扫描附近设备，只保留“候选条件满足且RSSI最强”的一个设备。
namespace {
class StrongestScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if(g_scanOwner) g_scanOwner->onScanResult(d);
  }
};
StrongestScanCallbacks g_strongestScanCallbacks;
}

uint8_t BmsBle::scanDevices(uint32_t sec){
  scanCount_=0;
  // 每轮扫描只保留“符合候选条件且RSSI最强”的一个设备。
  // 不保存其他设备，降低扫描结果RAM占用，也避免连接到较弱/错误的BMS。
  scanItems_[0].address[0]=0;
  scanItems_[0].name[0]=0;
  scanItems_[0].addressType=BLE_ADDR_PUBLIC;
  scanItems_[0].rssi=-127;

  NimBLEScan* s=NimBLEDevice::getScan();
  if(!s) return 0;

  s->setScanCallbacks(&g_strongestScanCallbacks, false);
  s->setMaxResults(0);
  s->setActiveScan(true);
  s->setInterval(80);
  s->setWindow(60);

  if(!s->start(sec*1000UL, false, true)){
    s->stop();
    s->clearResults();
    return 0;
  }

  s->stop();
  s->clearResults();
  if(scanCount_>0){
    Serial.printf("JK BLE: strongest candidate %s type=%u RSSI=%d\n",
                  scanItems_[0].address,
                  scanItems_[0].addressType,
                  scanItems_[0].rssi);
  }
  return scanCount_;
}

// [流程2] 一次完整的“扫描 -> 选设备 -> 连接”业务。main.cpp负责最多调用3轮。
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

  // scanDevices() 已经只留下RSSI最强候选，无需再次排序/保存其他设备。
  return connectDeviceByIndex(0);
}

bool BmsBle::connectDeviceByIndex(uint8_t index){
  if(index>=scanCount_) return false;
  return connectByAddress(scanItems_[index].address,scanItems_[index].addressType);
}

// [流程3] 按MAC连接，并完成 GATT 服务、读写特征、通知订阅。
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

  // 与 dionipe/jk-bms-esp32-cyd 的 JK 连接参数保持一致：
  // interval=12, latency=12, supervision timeout=0, connection timeout=51。
  // 这样避免 V6 在 GATT 建链阶段与已验证的 JK 实现出现连接参数差异。
  client_->setConnectionParams(12,12,0,51);
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
  if(yanyangMode_) yanyangDecoder_.reset();
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
  // 连接阶段只记录当前候选地址，不立即写入Preferences。
  // 必须等收到有效BMS数据后，才正式保存快速连接目标。
  configuredAddressType_=addressType;
  g_bmsData.mac=address;
  g_bmsData.online=true;
  g_bmsData.bootState=BOOT_CONNECTED;
  g_bmsData.statusMessage="已连接JK电池";
  g_bmsData.scanAttempt=scanAttempt_;

  // --------------------------------------------------------------
  // JK 初始化流程严格按 dionipe/jk-bms-esp32-cyd 的连接顺序：
  //   subscribe -> 等待1000ms -> DEV_INFO(0x97) -> 等待800ms
  //   -> CELL_INFO(0x96) -> 等待800ms
  //
  // 0x97 / 0x96 的实际20字节帧由 JkProtocol::buildCommand() 生成，
  // BLE 层不再自己拼协议帧，保持 V6 的模块化架构。
  //
  // ANT/TT 只有在已经保存并锁定对应协议时才发送自己的查询，
  // 避免连接 JK 后同时混发三套协议。
  // --------------------------------------------------------------
  const char* proto=protocolName();
  delay(1000);  // dionipe: subscribe 后给 BMS 1 秒稳定时间

  if(strcmp(proto,"YANYANG")==0){
    requestYanyangStatus();
  } else if(strcmp(proto,"ANT")==0){
    requestAntStatus();
  } else if(strcmp(proto,"TT")==0){
    request(0x03);
  } else if(strcmp(proto,"JBD")==0 || strcmp(proto,"DALY")==0){
    request(0x03);
  } else {
    // JK：设备信息 -> 电芯/实时信息。
    // dionipe 的 CMD_DEV_INFO = 0x97，CMD_CELL_INFO = 0x96。
    request(0x97);
    delay(800);
    request(0x96);
    delay(800);
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
  if(yanyangMode_){ yanyangDecoder_.feed(d,n); return; }
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

      // 只有真正解析出有效BMS帧后，才保存当前候选MAC。
      if(g_bmsData.mac.length()){
        setConfiguredAddress(g_bmsData.mac,configuredAddressType_);
      }

      // 自动识别成功后，把协议写入 Preferences。
      // 只有协议真正发生变化时才写 Flash，避免每一帧都产生擦写。
      const char* detected=protocolManager_.protocolName();
      Preferences p;
      p.begin("jkcfg",false);
      String saved=p.getString("protocol","");
      if(detected && detected[0] && strcmp(detected,"NONE")!=0 && strcmp(detected,saved.c_str())!=0){
        p.putString("protocol",detected);
        Serial.printf("BMS protocol locked: %s\n",detected);
      }
      p.end();
    }

    memmove(g_rxBuf,g_rxBuf+expected,g_rxLen-expected);
    g_rxLen-=expected;
  }
  if(g_legacyAckSeen && g_rxLen==0){ lastRequest_=0; g_legacyAckSeen=false; }
}

void BmsBle::onYanyangStatus(const BmsData& data){
  // 更新遥测字段，不覆盖网页配置、MAC和启动状态等本机字段。
  g_bmsData.valid=data.valid;
  g_bmsData.online=true;
  g_bmsData.updateMs=data.updateMs;
  g_bmsData.cellCount=data.cellCount;
  for(uint8_t i=0;i<JK_MAX_CELLS;i++) g_bmsData.cellVoltage[i]=data.cellVoltage[i];
  g_bmsData.minCellVoltage=data.minCellVoltage;
  g_bmsData.maxCellVoltage=data.maxCellVoltage;
  g_bmsData.deltaCellVoltage=data.deltaCellVoltage;
  g_bmsData.minCell=data.minCell;
  g_bmsData.maxCell=data.maxCell;
  g_bmsData.totalVoltage=data.totalVoltage;
  g_bmsData.current=data.current;
  g_bmsData.power=data.power;
  g_bmsData.soc=data.soc;
  g_bmsData.temperature1=data.temperature1;
  g_bmsData.temperature2=data.temperature2;
  g_bmsData.mosTemperature=data.mosTemperature;
  g_bmsData.totalCapacityAh=data.totalCapacityAh;
  g_bmsData.remainingCapacityAh=data.remainingCapacityAh;
  g_bmsData.statusMessage="彦阳保护板数据已更新";
  if(instance_ && g_bmsData.mac.length() &&
     instance_->getConfiguredAddress()!=g_bmsData.mac)
    instance_->setConfiguredAddress(g_bmsData.mac,instance_->configuredAddressType_);
}

void BmsBle::onYanyangDeviceInfo(const char* hardwareVersion,const char* softwareVersion){
  Serial.printf("Yanyang BMS detected: HW=%s SW=%s\n",
                hardwareVersion ? hardwareVersion : "unknown",
                softwareVersion ? softwareVersion : "unknown");
}

void BmsBle::requestYanyangStatus(){
  if(!writeCh_ && !ch_) return;
  NimBLERemoteCharacteristic* writer=writeCh_ ? writeCh_ : ch_;
  uint8_t frame[8];
  YanyangProtocolDecoder::buildStatusRequest(0x01,frame);
  if(writer->canWriteNoResponse()) writer->writeValue(frame,sizeof(frame),false);
  else if(writer->canWrite()) writer->writeValue(frame,sizeof(frame),true);
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
  if(strcmp(protocolManager_.protocolName(),"TT")==0) n=8;
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
      // 运行期恢复次数必须使用 recoveryFailures_，不能复用启动扫描计数。
      // 否则启动阶段已经扫描到第3次后，运行期第一次断线恢复也会显示为3/3。
      uint8_t reconnectAttempt=(uint8_t)(recoveryFailures_+1);
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

  const char* proto=protocolManager_.protocolName();
  if(strcmp(proto,"YANYANG")==0){
    if(now-lastRequest_>=1000){ requestYanyangStatus(); lastRequest_=now; }
  } else if(strcmp(proto,"TT")==0){
    if(now-lastRequest_>=250){ request(0x03); lastRequest_=now; }
  } else if(strcmp(proto,"ANT")==0){
    if(now-lastRequest_>=2000){ requestAntStatus(); lastRequest_=now; }
  } else if(strcmp(proto,"JK")==0){
    // 与 dionipe 的常规 JK polling 保持一致：继续请求 CELL_INFO(0x96)。
    if(now-lastRequest_>=5000){ request(0x96); lastRequest_=now; }
  } else if(strcmp(proto,"JBD")==0 || strcmp(proto,"DALY")==0){
    if(now-lastRequest_>=2000){ request(0x03); lastRequest_=now; }
  }
}

bool BmsBle::connected() const{
  return client_ && client_->isConnected();
}

// [释放] 三次失败进入热点前，必须释放 BLE Client 和扫描资源。
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
  g_rxLen=0;
  g_legacyAckSeen=false;
  recoveryVerifyStart_=0;

  NimBLEScan* s=NimBLEDevice::getScan();
  if(s){
    s->stop();
    s->clearResults();
  }
}
