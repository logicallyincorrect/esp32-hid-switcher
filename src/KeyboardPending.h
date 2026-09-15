#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Keep press/release order under temporary transport backpressure.
class KeyboardPending {
  uint8_t reports[64][8]={},last[8]={};
  size_t head=0,count=0;
public:
  void clear(){head=count=0;memset(last,0,sizeof(last));}
  bool push(const uint8_t *report){
    const uint8_t *previous=count?reports[(head+count-1)%64]:last;
    if(memcmp(previous,report,8)==0)return true;
    if(count==64)return false;
    memcpy(reports[(head+count++)%64],report,8);return true;
  }
  const uint8_t *peek()const{return count?reports[head]:nullptr;}
  void accepted(){if(count){memcpy(last,reports[head],8);head=(head+1)%64;--count;}}
};
