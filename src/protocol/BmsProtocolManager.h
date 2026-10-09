#ifndef BMS_PROTOCOL_MANAGER_H
#define BMS_PROTOCOL_MANAGER_H
#include <Arduino.h>
#include "../BmsData.h"
#include "BmsProtocol.h"
#include "jk/JkProtocol.h"
#include "ant/AntProtocol.h"
#include "jbd/JbdProtocol.h"
#include "daly/DalyProtocol.h"
#include "tt/TtProtocol.h"

class BmsProtocolManager {
public:
  BmsProtocolManager();
  void begin(bool protocol32S);
  void setProtocol32S(bool enable);
  bool isProtocol32S() const;
  bool setPreferredProtocol(const String& name);
  const char* preferredProtocolName() const;
  bool parseFrame(const uint8_t*,size_t,BmsData&);
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) ;
  int findFrameStart(const uint8_t*,size_t);
  size_t expectedFrameLength() const;
  size_t frameLength(const uint8_t*,size_t) const;
  const char* protocolName() const;

private:
  JkProtocol jkProtocol_;
  AntProtocol antProtocol_;
  JbdProtocol jbdProtocol_;
  DalyProtocol dalyProtocol_;
  TtProtocol ttProtocol_;
  BmsProtocol* activeProtocol_;
  // 仅用于当前接收缓冲区帧长计算；解析成功后才更新 activeProtocol_。
  BmsProtocol* frameProtocol_;

  BmsProtocol* detectProtocol(const uint8_t*,size_t);
};

#endif
