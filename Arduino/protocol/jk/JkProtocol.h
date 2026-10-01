#ifndef JK_PROTOCOL_H
#define JK_PROTOCOL_H
#include <Arduino.h>
#include "../BmsProtocol.h"
#include "Jk02_24S.h"
#include "Jk02_32S.h"
class JkProtocol : public BmsProtocol {
public:
  static const size_t FRAME_MAX=360;
  JkProtocol();
  void setProtocol32S(bool enable){protocol32S_=enable;}
  bool isProtocol32S() const {return protocol32S_;}
  const char* name() const override {return protocol32S_ ? Jk02_32S::name() : Jk02_24S::name();}
  bool canHandle(const uint8_t*,size_t) const override;
  int findFrameStart(const uint8_t*,size_t) const override;
  bool parseFrame(const uint8_t*,size_t,BmsData&) override;
  bool buildCommand(uint8_t,uint8_t,uint8_t[20]) override;
  size_t expectedFrameLength() const override {return 300;}
private:
  bool protocol32S_;
  static uint16_t u16le(const uint8_t*);
  static uint32_t u32le(const uint8_t*);
  static int16_t s16le(const uint8_t*);
  static bool validCrc(const uint8_t*,size_t);
  int detectOffset(const uint8_t*,size_t) const;
  bool parseMainFrame(const uint8_t*,size_t,BmsData&);
};
#endif
