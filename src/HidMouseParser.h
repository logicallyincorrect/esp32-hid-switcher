#pragma once
#include "MouseReport.h"
#include <stddef.h>
#include <string.h>

// Bounded HID short-item parser for relative mice. Reject unsupported layouts.
class HidMouseParser {
  struct Field { uint16_t offset=0; uint8_t size=0; bool sign=false; };
  struct Layout { uint8_t id=0; uint16_t bits=0; Field x,y,wheel,pan,buttons[8]; bool used=false; };
  Layout layouts[8] = {};
  bool ids=false;
  static int32_t extract(const uint8_t *p, Field f) {
    uint32_t v=0;
    for (unsigned i=0;i<f.size;++i) v |= uint32_t((p[(f.offset+i)/8] >> ((f.offset+i)%8)) & 1) << i;
    if (f.sign && f.size && (v & (1u << (f.size-1)))) v |= ~((1u << f.size)-1);
    return int32_t(v);
  }
public:
  bool supports(uint8_t id)const{
    for(const auto &l:layouts)if(l.used&&l.id==id&&(l.x.size||l.y.size||l.wheel.size||l.pan.size||l.buttons[0].size))return true;
    return false;
  }
  bool parse(const uint8_t *data, size_t length) {
    *this = {};
    struct Global { uint32_t page=0,size=0,count=0,id=0; int32_t min=0; } g,stack[4];
    unsigned depth=0,collectionDepth=0; bool mouseStack[16]={},mouse=false;
    uint32_t usages[32]={},usageMin=0,usageMax=0; unsigned n=0; bool range=false;
    auto layout=[&]() -> Layout* {
      for(auto &l:layouts) if(l.used && l.id==g.id) return &l;
      for(auto &l:layouts) if(!l.used) {l.used=true;l.id=g.id;return &l;}
      return nullptr;
    };
    for(size_t pos=0;pos<length;) {
      uint8_t tag=data[pos++]; if(tag==0xfe) return false;
      unsigned size=tag&3; if(size==3)size=4;
      if(pos+size>length)return false;
      uint32_t v=0; for(unsigned i=0;i<size;++i)v|=uint32_t(data[pos++])<<(8*i);
      const unsigned type=(tag>>2)&3,item=tag>>4;
      if(type==1) {
        switch(item) {
          case 0:g.page=v;break;
          case 1:g.min=size==1?int8_t(v):(size==2?int16_t(v):int32_t(v));break;
          case 7:if(v>32)return false;g.size=v;break;
          case 8:if(v==0||v>255)return false;ids=true;g.id=v;break;
          case 9:if(v>256)return false;g.count=v;break;
          case 10:if(depth==4)return false;stack[depth++]=g;break;
          case 11:if(!depth)return false;g=stack[--depth];break;
        }
      } else if(type==2) {
        if(item==0) {if(n==32)return false;usages[n++]=v;}
        else if(item==1){usageMin=v;range=true;}
        else if(item==2)usageMax=v;
      } else if(type==0) {
        if(item==10) {
          if(collectionDepth==16)return false;
          mouseStack[collectionDepth++]=mouse;
          if(v==1) mouse=(g.page==1 && n && usages[0]==2);
        } else if(item==12) {
          if(!collectionDepth)return false;
          mouse=mouseStack[--collectionDepth];
        } else if(item==8) {
          auto l=layout(); if(!l||l->bits+g.size*g.count>512)return false;
          if(mouse && !(v&1) && (v&2)) {
            for(unsigned i=0;i<g.count;++i) {
              uint32_t u=i<n?usages[i]:(range && usageMin+i<=usageMax?usageMin+i:0);
              uint32_t page=g.page; if(u>65535){page=u>>16;u&=65535;}
              Field f{uint16_t(l->bits+i*g.size),uint8_t(g.size),g.min<0};
              if(page==9 && u>=1 && u<=8 && g.size==1) l->buttons[u-1]=f;
              else if((v&4) && g.size>0 && g.size<=16) {
                if(page==1 && u==0x30)l->x=f;
                if(page==1 && u==0x31)l->y=f;
                if(page==1 && u==0x38)l->wheel=f;
                if(page==12 && u==0x238)l->pan=f;
              }
            }
          }
          l->bits+=g.size*g.count;
        }
        n=0;range=false;usageMin=usageMax=0;
      }
    }
    if(depth||collectionDepth)return false;
    for(auto &l:layouts)if(l.x.size && l.y.size)return true;
    return false;
  }
  bool decode(const uint8_t *data,size_t length,MouseReport &out,bool &hasButtons) const {
    uint8_t id=0;if(ids){if(!length)return false;id=*data++;--length;}
    for(const auto &l:layouts)if(l.used && l.id==id) {
      if(length*8<l.bits)return false;
      if(!l.x.size&&!l.y.size&&!l.wheel.size&&!l.pan.size&&!l.buttons[0].size)return false;
      out={};hasButtons=false;
      int x=extract(data,l.x),y=extract(data,l.y);
      out.x=x>32767?32767:(x< -32767?-32767:x);out.y=y>32767?32767:(y< -32767?-32767:y);
      int w=extract(data,l.wheel),p=extract(data,l.pan);
      out.wheel=w>127?127:(w< -127?-127:w);out.pan=p>127?127:(p< -127?-127:p);
      for(unsigned i=0;i<8;++i)if(l.buttons[i].size){hasButtons=true;if(extract(data,l.buttons[i]))out.buttons|=1u<<i;}
      return true;
    }
    return false;
  }
};
