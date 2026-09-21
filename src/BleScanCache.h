#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Merge advertising and scan-response data independently of their arrival
// order. Missing fields in a later packet must not erase learned identity.
struct BleScanCache {
  struct Entry {uint8_t address[6]{},type=0;bool connectable=false,hid=false,completeName=false;char name[41]{};};
  Entry entries[96]{};unsigned count=0;bool limited=false;
  void clear(){count=0;limited=false;}
  void observe(const uint8_t *address,uint8_t type,bool connectable,const uint8_t *data,size_t size){
    unsigned index=0;for(;index<count;++index)if(entries[index].type==type&&!memcmp(entries[index].address,address,6))break;
    if(index==count){if(count==96){limited=true;return;}entries[index]={};memcpy(entries[index].address,address,6);entries[index].type=type;++count;}
    auto &entry=entries[index];entry.connectable|=connectable;
    for(size_t pos=0;pos<size;){
      const unsigned length=data[pos++];if(!length)break;if(length>size-pos)break;
      const auto kind=data[pos];const auto value=data+pos+1;const size_t bytes=length-1;
      if((kind==8||kind==9)&&bytes&&(kind==9||!entry.completeName)){
        const size_t n=bytes<40?bytes:40;for(size_t i=0;i<n;++i)entry.name[i]=value[i]>=32&&value[i]<=126?char(value[i]):'?';entry.name[n]=0;entry.completeName=kind==9;
      }
      if(kind==2||kind==3)for(size_t i=0;i+1<bytes;i+=2)if(value[i]==0x12&&value[i+1]==0x18)entry.hid=true;
      if(kind==6||kind==7){
        const uint8_t hid[16]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x12,0x18,0,0};
        for(size_t i=0;i+15<bytes;i+=16)if(!memcmp(value+i,hid,16))entry.hid=true;
      }
      if(kind==0x19&&bytes>=2&&((uint16_t(value[0])|(uint16_t(value[1])<<8))&0xffc0)==0x03c0)entry.hid=true;
      pos+=length;
    }
  }
};
