#ifndef JK02_32S_H
#define JK02_32S_H
#include <Arduino.h>
class Jk02_32S {
public:
  static const uint8_t MAX_CELLS=32;
  static const int DATA_OFFSET=32;
  static const size_t FRAME_LENGTH=300;
  static const char* name(){return "JK02_32S";}
};
#endif
