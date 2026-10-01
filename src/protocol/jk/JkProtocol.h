#ifndef JK_PROTOCOL_H
#define JK_PROTOCOL_H

#include <Arduino.h>
#include "../BmsProtocol.h"
#include "Jk02_24S.h"
#include "Jk02_32S.h"

class JkProtocol : public BmsProtocol {
public:
  static const size_t FRAME_MAX = 700;

  JkProtocol();

  void setProtocol32S(bool enable){ protocol32S_ = enable; }
  bool isProtocol32S() const { return protocol32S_; }

  const char* name() const override;
  bool canHandle(const uint8_t*, size_t) const override;
  int findFrameStart(const uint8_t*, size_t) const override;
  bool parseFrame(const uint8_t*, size_t, BmsData&) override;
  bool buildCommand(uint8_t, uint8_t, uint8_t[20]) override;
  size_t expectedFrameLength() const override { return 300; }
  size_t frameLength(const uint8_t*, size_t) const override;

private:
  bool protocol32S_;

  static uint16_t u16le(const uint8_t*);
  static uint32_t u32le(const uint8_t*);
  static int16_t s16le(const uint8_t*);
  static uint32_t u32be(const uint8_t*);
  static int32_t s32be(const uint8_t*);
  static int tagSize(uint8_t tag);

  int detectOffset(const uint8_t*, size_t) const;
  bool parseOldFrame(const uint8_t*, size_t, BmsData&);
  bool parseNewTlvFrame(const uint8_t*, size_t, BmsData&);
  bool parseLegacyRs485Frame(const uint8_t*, size_t, BmsData&);
};

#endif
