/*
 * ================================================================
 * BmsProtocolManager.cpp
 *
 * 作用：协议“总调度员”。
 *
 * 原始数据进来后：
 *   ① 找帧头
 *   ② 判断完整长度
 *   ③ 根据帧头识别协议
 *   ④ 把完整帧交给具体协议类
 *   ⑤ 具体协议更新 g_bmsData
 *
 * 注意：这里不解析具体电压/电流字节。
 * 那些属于 JK/ANT/JBD/Daly/TT 各自的协议知识。
 * ================================================================
 */

#include "BmsProtocolManager.h"

BmsProtocolManager::BmsProtocolManager() : activeProtocol_(nullptr) {}

// [初始化] 默认从 JK 开始；收到其他协议帧后 activeProtocol_ 会自动切换。
void BmsProtocolManager::begin(bool protocol32S) {
  jkProtocol_.setProtocol32S(protocol32S);
  activeProtocol_ = &jkProtocol_;
}

void BmsProtocolManager::setProtocol32S(bool enable) {
  jkProtocol_.setProtocol32S(enable);
}

bool BmsProtocolManager::isProtocol32S() const {
  return jkProtocol_.isProtocol32S();
}

bool BmsProtocolManager::setPreferredProtocol(const String& name) {
  String n=name;
  n.trim();
  n.toUpperCase();
  if(n=="JK") { activeProtocol_=&jkProtocol_; return true; }
  if(n=="ANT") { activeProtocol_=&antProtocol_; return true; }
  if(n=="JBD") { activeProtocol_=&jbdProtocol_; return true; }
  if(n=="DALY") { activeProtocol_=&dalyProtocol_; return true; }
  if(n=="TT" || n=="IRON_TOWER") { activeProtocol_=&ttProtocol_; return true; }
  return false;
}

const char* BmsProtocolManager::preferredProtocolName() const {
  return activeProtocol_ ? activeProtocol_->name() : "NONE";
}

// [分支] 按顺序尝试识别协议。第一个能处理该帧的协议获胜。
BmsProtocol* BmsProtocolManager::detectProtocol(const uint8_t* d,size_t n) {
  if (jkProtocol_.canHandle(d,n)) return &jkProtocol_;
  if (antProtocol_.canHandle(d,n)) return &antProtocol_;
  if (jbdProtocol_.canHandle(d,n)) return &jbdProtocol_;
  if (dalyProtocol_.canHandle(d,n)) return &dalyProtocol_;
  if (ttProtocol_.canHandle(d,n)) return &ttProtocol_;
  return nullptr;
}

// [入口] 已经拿到完整帧后，从这里进入具体协议解析器。
bool BmsProtocolManager::parseFrame(const uint8_t* d,size_t n,BmsData&o) {
  BmsProtocol* p=detectProtocol(d,n);
  if(!p) return false;
  activeProtocol_=p;
  return activeProtocol_->parseFrame(d,n,o);
}

bool BmsProtocolManager::buildCommand(uint8_t c,uint8_t n,uint8_t out[20]) {
  if(!activeProtocol_) activeProtocol_=&jkProtocol_;
  return activeProtocol_->buildCommand(c,n,out);
}

// [入口] BLE层不知道协议帧头，所以由这里统一搜索。
int BmsProtocolManager::findFrameStart(const uint8_t*d,size_t n) {
  int best=-1;
  BmsProtocol* list[]={&jkProtocol_,&antProtocol_,&jbdProtocol_,&dalyProtocol_,&ttProtocol_};
  for(size_t i=0;i<sizeof(list)/sizeof(list[0]);i++){
    int s=list[i]->findFrameStart(d,n);
    if(s>=0&&(best<0||s<best)){
      best=s;
      activeProtocol_=list[i];
    }
  }
  return best;
}

size_t BmsProtocolManager::expectedFrameLength() const {
  return activeProtocol_ ? activeProtocol_->expectedFrameLength() : 300;
}

// [入口] 固定长度协议和4E57变长JK协议统一从这里获取实际帧长。
size_t BmsProtocolManager::frameLength(const uint8_t* p,size_t n) const {
  return activeProtocol_ ? activeProtocol_->frameLength(p,n) : 0;
}

const char* BmsProtocolManager::protocolName() const {
  return activeProtocol_ ? activeProtocol_->name() : "NONE";
}
