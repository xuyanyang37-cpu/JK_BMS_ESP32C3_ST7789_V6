/*
 * ================================================================
 * BmsProtocolManager.cpp
 * 协议总调度：找帧头 -> 确定该帧协议 -> 获取长度 -> 解析完整帧。
 * activeProtocol_ 必须始终对应 findFrameStart() 选中的最早帧头。
 * ================================================================
 */
#include "BmsProtocolManager.h"

BmsProtocolManager::BmsProtocolManager() : activeProtocol_(nullptr), frameProtocol_(nullptr) {}

void BmsProtocolManager::begin(bool protocol32S) {
  jkProtocol_.setProtocol32S(protocol32S);
  activeProtocol_ = &jkProtocol_;
  frameProtocol_ = nullptr;
}

void BmsProtocolManager::setProtocol32S(bool enable) {
  jkProtocol_.setProtocol32S(enable);
}

bool BmsProtocolManager::isProtocol32S() const {
  return jkProtocol_.isProtocol32S();
}

bool BmsProtocolManager::setPreferredProtocol(const String& name) {
  String n = name;
  n.trim();
  n.toUpperCase();
  if (n == "JK") { activeProtocol_ = &jkProtocol_; frameProtocol_ = nullptr; return true; }
  if (n == "ANT") { activeProtocol_ = &antProtocol_; frameProtocol_ = nullptr; return true; }
  if (n == "JBD") { activeProtocol_ = &jbdProtocol_; frameProtocol_ = nullptr; return true; }
  if (n == "DALY") { activeProtocol_ = &dalyProtocol_; frameProtocol_ = nullptr; return true; }
  if (n == "TT" || n == "IRON_TOWER") { activeProtocol_ = &ttProtocol_; frameProtocol_ = nullptr; return true; }
  return false;
}

const char* BmsProtocolManager::preferredProtocolName() const {
  return activeProtocol_ ? activeProtocol_->name() : "NONE";
}

BmsProtocol* BmsProtocolManager::detectProtocol(const uint8_t* d, size_t n) {
  if (jkProtocol_.canHandle(d, n)) return &jkProtocol_;
  if (antProtocol_.canHandle(d, n)) return &antProtocol_;
  if (jbdProtocol_.canHandle(d, n)) return &jbdProtocol_;
  if (dalyProtocol_.canHandle(d, n)) return &dalyProtocol_;
  if (ttProtocol_.canHandle(d, n)) return &ttProtocol_;
  return nullptr;
}

bool BmsProtocolManager::parseFrame(const uint8_t* d, size_t n, BmsData& o) {
  BmsProtocol* p = detectProtocol(d, n);
  if (!p) return false;
  // 只有解析器确认完整帧有效后才锁定当前协议，避免坏帧误切换协议。
  if (!p->parseFrame(d, n, o)) { frameProtocol_ = nullptr; return false; }
  activeProtocol_ = p;
  frameProtocol_ = nullptr;
  return true;
}

bool BmsProtocolManager::buildCommand(uint8_t c, uint8_t counter, uint8_t out[20]) {
  if (!activeProtocol_) activeProtocol_ = &jkProtocol_;
  return activeProtocol_->buildCommand(c, counter, out);
}

int BmsProtocolManager::findFrameStart(const uint8_t* d, size_t n) {
  if (!d || n == 0) return -1;
  int best = -1;
  BmsProtocol* bestProtocol = nullptr;
  BmsProtocol* list[] = { &jkProtocol_, &antProtocol_, &jbdProtocol_, &dalyProtocol_, &ttProtocol_ };
  for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i) {
    int start = list[i]->findFrameStart(d, n);
    if (start >= 0 && (best < 0 || start < best)) {
      best = start;
      bestProtocol = list[i];
    }
  }
  // 关键修复：不能因为后面某个协议也找到帧头，就覆盖最早帧头对应的协议。
  // 帧头候选只影响本帧长度计算，不立即切换活动协议/后续命令。
  frameProtocol_ = bestProtocol;
  return best;
}

size_t BmsProtocolManager::expectedFrameLength() const {
  return activeProtocol_ ? activeProtocol_->expectedFrameLength() : 0;
}

size_t BmsProtocolManager::frameLength(const uint8_t* p, size_t n) const {
  BmsProtocol* parser = frameProtocol_ ? frameProtocol_ : activeProtocol_;
  return parser ? parser->frameLength(p, n) : 0;
}

const char* BmsProtocolManager::protocolName() const {
  return activeProtocol_ ? activeProtocol_->name() : "NONE";
}
