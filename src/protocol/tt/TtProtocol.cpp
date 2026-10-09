#include "TtProtocol.h"
#include <string.h>

uint16_t TtProtocol::crc16(const uint8_t*p,size_t n){
  uint16_t c=0xFFFF;
  while(n--){ c^=*p++; for(uint8_t b=0;b<8;b++) c=(c&1)?uint16_t((c>>1)^0xA001):uint16_t(c>>1); }
  return c;
}
uint16_t TtProtocol::be16(const uint8_t*p){return uint16_t(p[0]<<8|p[1]);}
uint32_t TtProtocol::be32(const uint8_t*p){return (uint32_t(be16(p))<<16)|be16(p+2);}
int16_t TtProtocol::decodeCurrent(uint16_t v){
  return v<50000 ? int16_t(v) : int16_t(int32_t(v)-65536);
}
int TtProtocol::decodeTemp(uint16_t raw){
  if(raw<=200) return raw>100 ? 100-int(raw) : int(raw);
  int16_t s=(int16_t)raw;
  if(s>=-100&&s<=100) return s;
  return -128;
}
uint16_t TtProtocol::efSum(const uint8_t*p,size_t n){
  uint32_t s=0; for(size_t i=1;i+3<n;i++) s+=p[i];
  return uint16_t(0x10000-(s&0xFFFF));
}
bool TtProtocol::canHandle(const uint8_t*d,size_t n) const{
  return n>=3 && ((d[0]==0x01&&(d[1]==0x03||d[1]==0x01||(d[1]&0x80)))||d[0]==0xEF);
}
int TtProtocol::findFrameStart(const uint8_t*d,size_t n) const{
  if(!d) return -1;
  for(size_t i=0;i+1<n;i++)
    if((d[i]==0x01&&(d[i+1]==0x03||d[i+1]==0x01||(d[i+1]&0x80)))||d[i]==0xEF){ if(d[i]==0xEF && i+3<n) expectedLen_=7+d[i+3]; else if(i+2<n) expectedLen_=5+d[i+2]; return int(i); }
  return -1;
}
size_t TtProtocol::frameLength(const uint8_t* d,size_t n) const {
  if(!d || n<2) return 0;
  // Modbus RTU exception: slave + function|0x80 + exception + CRC16.
  if(d[0]==0x01 && (d[1]&0x80U)) return 5;
  if(d[0]==0x01 && (d[1]==0x03 || d[1]==0x01)) {
    if(n<3) return 0;
    if(d[2]>64) return 0;
    return size_t(5U+d[2]);
  }
  // EF proprietary frame: 7-byte overhead plus the length field at byte 3.
  if(d[0]==0xEF) {
    if(n<4) return 0;
    size_t total=7U+size_t(d[3]);
    return total<=128U ? total : 0;
  }
  return 0;
}

bool TtProtocol::parseFrame(const uint8_t*d,size_t n,BmsData&o){
  if(!d||n<5) return false;
  if(d[0]==0xEF) return parseEf(d,n,o);
  if(n>=3 && d[1]&0x80){expectedLen_=5;return false;}
  if(n<5 || d[2]>64) return false;
  size_t fl=5+d[2];
  if(n<fl) return false;
  uint16_t got=uint16_t(d[fl-2])|(uint16_t(d[fl-1])<<8);
  if(crc16(d,fl-2)!=got) return false;
  expectedLen_=fl;
  return parseModbus(d,fl,o);
}
bool TtProtocol::parseModbus(const uint8_t* f,size_t n,BmsData& o){
  if(!f || n<5) return false;

  // TT设备信息响应：当前只更新设备名，不把“设备信息”误判为有效BMS数据。
  if(f[1]==0x03 && f[2]==24){
    char id[25];
    memcpy(id,f+3,24);
    id[24]=0;
    for(int i=23;i>=0 && id[i]==' ';--i) id[i]=0;
    o.deviceName=String(id);
    o.online=true;
    o.updateMs=millis();
    return false;
  }

  // TT报警/状态响应：同样不能作为完整BMS数据验证。
  if(f[1]==0x01 && f[2]==7){
    uint32_t ov=((uint32_t)(f[4]>>4) |
                 ((uint32_t)f[5]<<4) |
                 ((uint32_t)f[6]<<12)) & 0xFFFFF;
    uint32_t uv=((uint32_t)f[7] |
                 ((uint32_t)f[8]<<8) |
                 ((uint32_t)(f[9]&0x0F)<<16)) & 0xFFFFF;
    o.errors=(f[3]&0xFC) | ((uint32_t)(f[4]&0x0F)<<8);
    if(ov || uv) o.errors|=0x80000000UL;
    o.online=true;
    o.updateMs=millis();
    return false;
  }

  // 0x03 / 58字节寄存器响应：完整TT电池状态帧。
  if(f[1]!=0x03 || f[2]!=58 || n!=63) return false;
  // 使用临时副本解析，只有所有关键字段校验通过才提交，避免坏数据污染共享状态。
  BmsData d=o;

  uint16_t tv=be16(f+3);
  if(tv==0xFFFD){
    d.valid=false;
    d.online=true;
    return false;
  }

  uint8_t cells=uint8_t(be16(f+5));
  if(cells>20) cells=20;

  d.totalVoltage=tv*0.01f;
  d.cellCount=cells;
  d.soc=be16(f+7);
  if(d.soc>100) d.soc=100;
  d.remainingCapacityAh=be16(f+9)*0.01f;
  if(d.totalCapacityAh<=0.0f) d.totalCapacityAh=d.remainingCapacityAh;
  d.current=decodeCurrent(be16(f+13))*0.01f;

  int t1=decodeTemp(be16(f+15));
  int t2=decodeTemp(be16(f+17));
  int t3=decodeTemp(be16(f+19));
  d.temperature1=(t1==-128)?0:t1;
  d.temperature2=(t2==-128)?0:t2;
  d.mosTemperature=(t3==-128)?0:t3;

  float minV=100.0f,maxV=0.0f;
  uint8_t minC=0,maxC=0;
  for(uint8_t i=0;i<JK_MAX_CELLS;i++) d.cellVoltage[i]=0;

  for(uint8_t i=0;i<cells;i++){
    float v=be16(f+21+i*2)*0.001f;
    d.cellVoltage[i]=v;
    if(v>=0.5f && v<=6.0f){
      if(v<minV){minV=v;minC=i+1;}
      if(v>maxV){maxV=v;maxC=i+1;}
    }
  }

  d.minCellVoltage=(minV<100.0f)?minV:0.0f;
  d.maxCellVoltage=maxV;
  d.deltaCellVoltage=(maxV>0.0f && minV<100.0f)?maxV-minV:0.0f;
  d.minCell=minC;
  d.maxCell=maxC;
  d.power=d.totalVoltage*d.current;
  d.online=true;
  d.valid=(d.totalVoltage>0.5f && d.cellCount>0);
  d.updateMs=millis();

  if(d.energyConsumptionWhKm>1.0f)
    d.remainingRangeKm=(d.remainingCapacityAh*d.totalVoltage)/d.energyConsumptionWhKm;

  if(d.valid) o=d;
  return d.valid;
}
bool TtProtocol::parseEf(const uint8_t*f,size_t n,BmsData&o){
  if(n<7||f[0]!=0xEF||f[n-1]!=0x16)return false;
  size_t fl=7+f[3];if(n<fl)return false;
  uint16_t got=uint16_t(f[fl-3])<<8|f[fl-2];
  if(efSum(f,fl)!=got)return false;
  expectedLen_=fl;
  if(f[2]==0xCA&&f[3]>=9){o.updateMs=millis();o.online=true;return false;}
  return false;
}
bool TtProtocol::buildCommand(uint8_t cmd,uint8_t,uint8_t out[20]){
  memset(out,0,20);
  if(cmd==0x03){uint8_t q[]={0x01,0x03,0x00,0x00,0x00,0x1D};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;expectedLen_=63;return true;}
  if(cmd==0x18){uint8_t q[]={0x01,0x03,0x03,0xE8,0x00,0x0C};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;return true;}
  if(cmd==0x01){uint8_t q[]={0x01,0x01,0x00,0x00,0x00,0x34};memcpy(out,q,6);uint16_t c=crc16(out,6);out[6]=c&255;out[7]=c>>8;return true;}
  if(cmd==0xCA){uint8_t q[]={0xEF,0x01,0xCA,0x00,0xFF};memcpy(out,q,5);uint16_t c=efSum(out,7);out[5]=c>>8;out[6]=c&255;out[7]=0x16;return true;}
  return false;
}
size_t TtProtocol::commandLength(uint8_t cmd) const{return (cmd==0x03||cmd==0x18||cmd==0x01)?8:8;}
