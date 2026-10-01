#ifndef JK02_24S_H
#define JK02_24S_H
#include <Arduino.h>
class Jk02_24S {
public:
  static const uint8_t MAX_CELLS=24;
  static const int DATA_OFFSET=0;
  static const size_t FRAME_LENGTH=300;
  static const char* name(){return "JK02_24S";}
};
#endif
