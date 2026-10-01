#ifndef FONT_GB2312_H
#define FONT_GB2312_H
#include <Arduino.h>
#include <TFT_eSPI.h>
// V6 编译兼容版：保留原接口。
// 若项目已有完整 GB2312 字库，可直接替换本文件和 FontGB2312.cpp。
namespace FontGB2312 {
void drawText(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font=1);
void drawCenterString(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font=1);
}
#endif
