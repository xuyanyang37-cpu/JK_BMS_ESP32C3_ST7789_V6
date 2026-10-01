#include "AntProtocol.h"
#include <string.h>

uint16_t AntProtocol::crc16(const uint8_t* data,size_t len){
  uint16_t crc=0xFFFF;
  for(size_t i=0;i<len;i++){
    crc^=data[i];
    for(uint8_t b=0;b<8;b++)
      crc=(crc&1)?uint16_t((crc>>1)^0xA001):uint16_t(crc>>1);
  }
  return crc;
}

uint16_t AntProtocol::u16le(const uint8_t* p){
  return uint16_t(p[0]) | (uint16_t(p[1])<<8);
}

uint32_t AntProtocol::u32le(const uint8_t* p){
  return uint32_t(p[0]) | (uint32_t(p[1])<<8) |
         (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}

int16_t AntProtocol::s16le(const uint8_t* p){
  return int16_t(u16le(p));
}

bool AntProtocol::validFrame(const uint8_t* p,size_t n){
  if(!p || n<10) return false;
  if(p[0]!=0x7E || p[1]!=0xA1 || p[2]!=0x11) return false;
  size_t total=size_t(p[5])+10U;
  if(total!=n) return false;
  if(total<8) return false;
  uint16_t calc=crc16(p+1,total-5);
  uint16_t recv=uint16_t(p[total-4]) | (uint16_t(p[total-3])<<8);
  return calc==recv;
}

bool AntProtocol::canHandle(const uint8_t* p,size_t n) const{
  if(!p || n<3) return false;
  return p[0]==0x7E && p[1]==0xA1 && p[2]==0x11;
}

int AntProtocol::findFrameStart(const uint8_t* p,size_t n) const{
  if(!p || n<2) return -1;
  for(size_t i=0;i+1<n;i++)
    if(p[i]==0x7E && p[i+1]==0xA1) return int(i);
  return -1;
}

size_t AntProtocol::frameLength(const uint8_t* p,size_t n) const{
  if(!p || n<6) return 0;
  if(p[0]!=0x7E || p[1]!=0xA1) return 0;
  size_t total=size_t(p[5])+10U;
  if(total>700) return 0;
  return total;
}

bool AntProtocol::parseFrame(const uint8_t* p,size_t n,BmsData& o){
  if(!validFrame(p,n)) return false;

  uint8_t tempSensors=p[8];
  if(tempSensors>6) tempSensors=6;

  uint8_t cells=p[9];
  if(cells==0 || cells>JK_MAX_CELLS) return false;

  size_t off2=size_t(cells)*2U + size_t(tempSensors)*2U;
  if(34U+off2+58U>n) return false;

  BmsData d=o;
  d.cellCount=cells;

  for(uint8_t i=0;i<cells;i++)
    d.cellVoltage[i]=u16le(p+34U+i*2U)*0.001f;

  size_t tempBase=34U+size_t(cells)*2U;
  if(tempSensors>=1) d.temperature1=float(s16le(p+tempBase));
  if(tempSensors>=2) d.temperature2=float(s16le(p+tempBase+2));

  d.mosTemperature=float(s16le(p+34U+off2));
  d.totalVoltage=float(u16le(p+38U+off2))*0.01f;
  d.current=float(s16le(p+40U+off2))*0.1f;
  d.soc=float(u16le(p+42U+off2)&0xFF);

  if(d.soc>100) d.soc=100;

  d.charging=(p[46U+off2]==0x01);
  d.discharging=(p[47U+off2]==0x01);
  d.balancing=(p[48U+off2]==0x04);

  d.totalCapacityAh=float(u32le(p+50U+off2))*0.000001f;
  d.remainingCapacityAh=float(u32le(p+54U+off2))*0.000001f;

  if(d.current>-0.05f && d.current<0.05f){
    d.current=0.0f;
    d.charging=false;
    d.discharging=false;
  }

  d.power=d.totalVoltage*d.current;
  if(d.energyConsumptionWhKm>1.0f && d.totalVoltage>0.1f)
    d.remainingRangeKm=(d.remainingCapacityAh*d.totalVoltage)/d.energyConsumptionWhKm;

  float minV=100.0f,maxV=0.0f;
  uint8_t minC=0,maxC=0;
  for(uint8_t i=0;i<cells;i++){
    float v=d.cellVoltage[i];
    if(v>0.5f && v<6.0f){
      if(v<minV){minV=v;minC=i+1;}
      if(v>maxV){maxV=v;maxC=i+1;}
    }
  }
  d.minCellVoltage=minV<100.0f?minV:0.0f;
  d.maxCellVoltage=maxV;
  d.deltaCellVoltage=(maxV>0.0f&&minV<100.0f)?maxV-minV:0.0f;
  d.minCell=minC;
  d.maxCell=maxC;

  d.deviceName="ANT BMS";
  d.valid=(d.cellCount>0 && d.totalVoltage>0.5f);
  d.online=d.valid;
  d.updateMs=millis();

  if(d.valid) o=d;
  return d.valid;
}

bool AntProtocol::buildCommand(uint8_t,uint8_t,uint8_t[20]){
  // ANT 的真实命令由 BmsBle::requestAntStatus() 构造10字节帧。
  // 不在通用20字节命令接口里伪造ANT帧。
  return false;
}
