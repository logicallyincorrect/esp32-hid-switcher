#pragma once
#include <stdint.h>
#include <string>
#include "EdgeSettings.h"

struct PointerTuning {
  uint32_t version=2;
  uint32_t horizontal=100,vertical=100;
  uint32_t spanX=0,spanY=0; // raw counts measured between relative-mode corners
  static bool validValue(unsigned field,unsigned value){
    return field<2&&value>=1&&value<=1600;
  }
  bool valid()const{return version==2&&validValue(0,horizontal)&&validValue(1,vertical)&&((!spanX&&!spanY)||(spanX>=64&&spanY>=64&&spanX<=1000000&&spanY<=1000000));}
  unsigned get(unsigned field)const{return field==0?horizontal:vertical;}
  void set(unsigned field,unsigned value){if(field==0)horizontal=value;else vertical=value;}
  static bool parse(const std::string &text,unsigned field,unsigned &value){return EdgeSettings::parse(text,value)&&validValue(field,value);}
};

// Retain sub-unit movement instead of rounding every small report to zero.
// Reversing direction discards the old direction's fraction.
class FractionalScale {
  int remainder=0;
public:
  void clear(){remainder=0;}
  int apply(int value,int gain,int denominator=100){
    if((value<0&&remainder>0)||(value>0&&remainder<0))remainder=0;
    const int64_t total=int64_t(value)*gain+remainder;
    const int result=total/denominator;remainder=total%denominator;return result;
  }
};
