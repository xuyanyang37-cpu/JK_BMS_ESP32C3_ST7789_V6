#ifndef ANT_PROTOCOL_H
#define ANT_PROTOCOL_H
#include <Arduino.h>
#include "../BmsProtocol.h"
class AntProtocol : public BmsProtocol {
public:
  const char* name() const override { return "ANT"; }
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override { return frameLength_; }
  size_t frameLength(const uint8_t*,size_t) const override;
private:
  mutable size_t frameLength_=0;
  static uint16_t crc16(const uint8_t*,size_t);
  static uint16_t u16le(const uint8_t*);
  static uint32_t u32le(const uint8_t*);
  static int16_t s16le(const uint8_t*);
  static bool validFrame(const uint8_t*,size_t);
};
#endif
