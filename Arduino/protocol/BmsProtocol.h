#ifndef BMS_PROTOCOL_H
#define BMS_PROTOCOL_H
#include <Arduino.h>
#include "../BmsData.h"

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
#endif
