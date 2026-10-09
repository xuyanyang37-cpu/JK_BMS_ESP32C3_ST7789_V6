#ifndef TT_PROTOCOL_H
#define TT_PROTOCOL_H
#include <Arduino.h>
#include "../BmsProtocol.h"

class TtProtocol : public BmsProtocol {
public:
  const char* name() const override { return "TT"; }
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override { return expectedLen_; }
  size_t frameLength(const uint8_t*,size_t) const override;
  size_t commandLength(uint8_t cmd) const;
private:
  mutable size_t expectedLen_=63;
  static uint16_t crc16(const uint8_t*,size_t);
  static uint16_t be16(const uint8_t*);
  static uint32_t be32(const uint8_t*);
  static int16_t decodeCurrent(uint16_t);
  static int decodeTemp(uint16_t);
  static uint16_t efSum(const uint8_t*,size_t);
  bool parseModbus(const uint8_t*,size_t,BmsData&);
  bool parseEf(const uint8_t*,size_t,BmsData&);
};
#endif
