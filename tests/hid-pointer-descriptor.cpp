#define HID_ABSOLUTE_POINTER 1
#include "HidReportMap.h"
#include <cassert>
#include <cstddef>
struct Layout {
  unsigned bits[4]={},buttons[4]={};
  bool absoluteXY=false,relativeXY=false,wheel=false;
};
static Layout parse(const uint8_t *map,size_t length){
  Layout result;
  unsigned id=0,size=0,count=0,page=0,lastUsage=0;
  int minimum=0,maximum=0,physicalMinimum=0,physicalMaximum=0;
  for(size_t i=0;i<length;){
    const unsigned prefix=map[i++],n=(prefix&3)==3?4:(prefix&3),type=(prefix>>2)&3,tag=prefix>>4;
    assert(i+n<=length);unsigned value=0;for(unsigned b=0;b<n;++b)value|=unsigned(map[i++])<<(8*b);
    if(type==1){
      if(tag==0)page=value;
      if(tag==1)minimum=(n==1?int(int8_t(value)):(n==2?int(int16_t(value)):int(value)));
      if(tag==2)maximum=value;
      if(tag==3)physicalMinimum=value;
      if(tag==4)physicalMaximum=value;
      if(tag==7)size=value;
      if(tag==8)id=value;
      if(tag==9)count=value;
    }
    if(type==2&&tag==0)lastUsage=value;
    if(type==0&&tag==8){
      assert(id<4);result.bits[id]+=size*count;
      if(page==9&&!(value&1)){assert(id==2||id==3);result.buttons[id]+=size*count;}
      if(page==1&&lastUsage==0x31&&size==16&&count==2){
        if(id==2){
          assert(!(value&4)&&minimum==0&&maximum==32767);
          assert(physicalMinimum==0&&physicalMaximum==32767);
          result.absoluteXY=true;
        }
        if(id==3){assert((value&4)&&minimum==-32767&&maximum==32767);result.relativeXY=true;}
      }
      if(id==2&&page==1&&lastUsage==0x38&&!(value&1)){
        assert((value&4)&&size==8&&count==1&&minimum==-127&&maximum==127);
        assert(physicalMinimum==0&&physicalMaximum==0);result.wheel=true;
      }
    }
  }
  return result;
}
int main(){
  const auto main=parse(reportMap,sizeof(reportMap));
  assert(main.bits[1]==64&&main.bits[2]==48&&main.bits[3]==0);
  assert(main.absoluteXY&&!main.relativeXY&&main.wheel&&main.buttons[2]==5);
#if HID_ABSOLUTE_ONLY_TEST
  assert(PointerMode::absoluteOnly&&!PointerMode::relativeReport&&PointerMode::subscriptions==3);
#else
  const auto relative=parse(relativeReportMap,sizeof(relativeReportMap));
  assert(relative.bits[1]==0&&relative.bits[2]==0&&relative.bits[3]==56);
  assert(!relative.absoluteXY&&relative.relativeXY&&relative.buttons[3]==8);
  assert(PointerMode::relativeReport&&PointerMode::subscriptions==7);
#endif
}
