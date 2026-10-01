#include "FontGB2312.h"
namespace FontGB2312 {
void drawText(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font){
  s.setTextColor(fg,bg);
  s.drawString(text,x,y,font);
}
void drawCenterString(TFT_eSprite& s,int16_t x,int16_t y,const String& text,uint16_t fg,uint16_t bg,uint8_t font){
  s.setTextColor(fg,bg);
  s.drawCentreString(text,x,y,font);
}
}
