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

  // 返回当前缓冲区中完整帧所需的字节数。
  // 固定长度协议直接返回 expectedFrameLength()；
  // 变长协议（例如 JK 4E57）根据帧头长度字段计算。
  virtual size_t frameLength(const uint8_t* p,size_t n) const {
    (void)p; (void)n;
    return expectedFrameLength();
  }
};
#endif
