#include "HidKeyboardParser.h"
#include "InputSources.h"
#include <cassert>
int main(){
  const uint8_t bootMap[]={
    5,1,9,6,0xa1,1,0x85,5,5,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};
  HidKeyboardParser parser;assert(parser.parse(bootMap,sizeof(bootMap)));
  uint8_t report[8]={2,0,4,5},out[8];assert(parser.decode(5,report,8,out));assert(out[0]==2&&out[2]==4&&out[3]==5);
  assert(!parser.decode(4,report,8,out));assert(!parser.decode(5,report,7,out));
  uint8_t prefixed[9]={5,2,0,4};assert(!parser.decode(5,prefixed,9,out)); // HOGP ID is not in payload
  report[2]=1;assert(parser.decode(5,report,8,out));for(unsigned i=2;i<8;++i)assert(out[i]==1);
  assert(!parser.parse(bootMap,sizeof(bootMap)-1));
  const uint8_t nkro[]={5,1,9,6,0xa1,1,5,7,0x19,4,0x29,11,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0xc0};
  assert(parser.parse(nkro,sizeof(nkro)));uint8_t bits=3;assert(parser.decode(0,&bits,1,out));assert(out[2]==4&&out[3]==5);
  bits=255;assert(parser.decode(0,&bits,1,out));for(unsigned i=2;i<8;++i)assert(out[i]==1);
  const uint8_t touch[]={5,0x0d,9,4,0xa1,1,5,7,0x19,4,0x29,11,0x75,1,0x95,8,0x81,2,0xc0};
  assert(!parser.parse(touch,sizeof(touch)));
  InputSources<9> sources;uint8_t usb[8]={2,0,4},ble[8]={1,0,4,5},zero[8]={};
  sources.keyboard(0,usb);sources.keyboard(1,ble);sources.combined(out);assert(out[0]==3&&out[2]==4&&out[3]==5);
  sources.keyboard(0,zero);sources.combined(out);assert(out[0]==1&&out[2]==4); // USB release cannot clear BLE hold
  assert(sources.mouse(0,1)==1);assert(sources.mouse(1,2)==3);assert(sources.mouse(0,0)==2);
  sources.clear(1);sources.combined(out);for(auto b:out)assert(!b);assert(!sources.mouseButtons());
  sources.keyboard(0,usb);sources.keyboard(1,ble);sources.clear(1);sources.combined(out);assert(out[0]==2&&out[2]==4&&!out[3]);
}
