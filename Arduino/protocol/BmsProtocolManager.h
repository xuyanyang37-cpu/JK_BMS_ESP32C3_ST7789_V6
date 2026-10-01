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
#endif
