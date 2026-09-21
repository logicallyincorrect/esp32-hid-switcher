#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// HOGP report characteristic payloads exclude the report ID. Support keyboard
// arrays and one-bit NKRO fields, ignoring consumer/vendor collections.
class HidKeyboardParser {
  struct Layout { uint16_t bits=0,offsets[256]={},arrayOffset=0;uint8_t id=0,arrayCount=0;bool used=false,keyboard=false; } layouts[4];
public:
  bool supports(uint8_t id)const{for(const auto &l:layouts)if(l.used&&l.id==id&&l.keyboard)return true;return false;}
  bool parse(const uint8_t *data,size_t length){
    *this={};
    struct Global {uint32_t page=0,size=0,count=0,id=0;int32_t min=0;} g,stack[4];
    unsigned depth=0,cd=0,n=0;bool keyboard=false,collections[16]={},range=false;
    uint32_t usages[32]={},lo=0,hi=0;
    for(size_t pos=0;pos<length;){
      const uint8_t prefix=data[pos++];if(prefix==0xfe)return false;
      unsigned bytes=prefix&3;if(bytes==3)bytes=4;if(pos+bytes>length)return false;
      uint32_t v=0;for(unsigned i=0;i<bytes;++i)v|=uint32_t(data[pos++])<<(i*8);
      const unsigned type=(prefix>>2)&3,tag=prefix>>4;
      if(type==1){switch(tag){
        case 0:g.page=v;break;
        case 1:g.min=bytes==1?int8_t(v):bytes==2?int16_t(v):int32_t(v);break;
        case 7:if(v>32)return false;g.size=v;break;
        case 8:if(!v||v>255)return false;g.id=v;break;
        case 9:if(v>256)return false;g.count=v;break;
        case 10:if(depth==4)return false;stack[depth++]=g;break;
        case 11:if(!depth)return false;g=stack[--depth];break;
      }}else if(type==2){if(tag==0){if(n==32)return false;usages[n++]=v;}else if(tag==1){lo=v;range=true;}else if(tag==2)hi=v;
      }else if(type==0){
        if(tag==10){if(cd==16)return false;collections[cd++]=keyboard;if(v==1)keyboard=g.page==1&&n&&usages[0]==6;}
        else if(tag==12){if(!cd)return false;keyboard=collections[--cd];}
        else if(tag==8){
          Layout *l=nullptr;for(auto &item:layouts)if(item.used&&item.id==g.id)l=&item;
          if(!l)for(auto &item:layouts)if(!item.used){l=&item;item.used=true;item.id=g.id;break;}
          if(!l||l->bits+g.size*g.count>512)return false;
          if(keyboard&&g.page==7&&!(v&1)){
            if(v&2){
              if(g.size!=1)return false;
              for(unsigned i=0;i<g.count;++i){const auto u=i<n?usages[i]:(range&&lo+i<=hi?lo+i:0);if(u>255)return false;if(u){l->offsets[u]=l->bits+i+1;l->keyboard=true;}}
            }else{
              // Keyboard selector values equal usages only for zero-based arrays.
              if(g.size!=8||g.count>32||g.min!=0||!range||lo!=0||hi>255||l->arrayCount)return false;
              l->arrayOffset=l->bits;l->arrayCount=g.count;l->keyboard=true;
            }
          }
          l->bits+=g.size*g.count;
        }
        n=0;range=false;lo=hi=0;
      }
    }
    if(depth||cd)return false;
    for(const auto &l:layouts)if(l.keyboard)return true;return false;
  }
  bool decode(uint8_t id,const uint8_t *data,size_t length,uint8_t out[8])const{
    const Layout *l=nullptr;for(const auto &item:layouts)if(item.used&&item.id==id&&item.keyboard)l=&item;
    if(!l||length!=(l->bits+7u)/8u)return false;
    bool keys[256]={};memset(out,0,8);
    for(unsigned u=1;u<256;++u)if(l->offsets[u]){auto b=l->offsets[u]-1;keys[u]=(data[b/8]>>(b%8))&1;}
    for(unsigned i=0;i<l->arrayCount;++i){unsigned value=0;for(unsigned j=0;j<8;++j){auto b=l->arrayOffset+i*8+j;value|=((data[b/8]>>(b%8))&1)<<j;}keys[value]=true;}
    for(unsigned i=0;i<8;++i)if(keys[224+i])out[0]|=1u<<i;
    unsigned count=0;for(unsigned k=4;k<224;++k)if(keys[k]){if(count<6)out[2+count]=k;++count;}
    if(keys[1]||keys[2]||keys[3]||count>6)memset(out+2,1,6);
    return true;
  }
};
