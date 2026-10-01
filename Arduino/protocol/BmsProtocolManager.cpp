#include "BmsProtocolManager.h"
BmsProtocolManager::BmsProtocolManager():activeProtocol_(nullptr){}
void BmsProtocolManager::begin(bool protocol32S){jkProtocol_.setProtocol32S(protocol32S);activeProtocol_=&jkProtocol_;}
void BmsProtocolManager::setProtocol32S(bool enable){jkProtocol_.setProtocol32S(enable);}
bool BmsProtocolManager::isProtocol32S() const{return jkProtocol_.isProtocol32S();}
BmsProtocol* BmsProtocolManager::detectProtocol(const uint8_t*d,size_t n){
  if(jkProtocol_.canHandle(d,n)) return &jkProtocol_;
  if(antProtocol_.canHandle(d,n)) return &antProtocol_;
  if(jbdProtocol_.canHandle(d,n)) return &jbdProtocol_;
  if(dalyProtocol_.canHandle(d,n)) return &dalyProtocol_;
  if(ttProtocol_.canHandle(d,n)) return &ttProtocol_;
  return nullptr;
}
bool BmsProtocolManager::parseFrame(const uint8_t*d,size_t n,BmsData&o){
  BmsProtocol*p=detectProtocol(d,n); if(!p) return false;
  activeProtocol_=p; return activeProtocol_->parseFrame(d,n,o);
}
bool BmsProtocolManager::buildCommand(uint8_t c,uint8_t n,uint8_t out[20]){
  if(!activeProtocol_) activeProtocol_=&jkProtocol_;
  return activeProtocol_->buildCommand(c,n,out);
}
int BmsProtocolManager::findFrameStart(const uint8_t*d,size_t n){
  int best=-1; BmsProtocol* list[]={&jkProtocol_,&antProtocol_,&jbdProtocol_,&dalyProtocol_,&ttProtocol_};
  for(size_t i=0;i<sizeof(list)/sizeof(list[0]);i++){
    int s=list[i]->findFrameStart(d,n);
    if(s>=0&&(best<0||s<best)){ best=s; activeProtocol_=list[i]; }
  }
  return best;
}
size_t BmsProtocolManager::expectedFrameLength() const{return activeProtocol_?activeProtocol_->expectedFrameLength():300;}
const char* BmsProtocolManager::protocolName() const{return activeProtocol_?activeProtocol_->name():"NONE";}
