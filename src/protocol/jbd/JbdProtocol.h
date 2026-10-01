#ifndef JBD_PROTOCOL_H
#define JBD_PROTOCOL_H
#include <Arduino.h>
#include "../BmsProtocol.h"
class JbdProtocol : public BmsProtocol {
public:
  const char* name() const override{return "JBD";}
  bool canHandle(const uint8_t*,size_t) const override{return false;}
  int findFrameStart(const uint8_t*,size_t) const override{return -1;}
  bool parseFrame(const uint8_t*,size_t,BmsData&) override{return false;}
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override{return false;}
  size_t expectedFrameLength() const override{return 0;}
};
#endif
