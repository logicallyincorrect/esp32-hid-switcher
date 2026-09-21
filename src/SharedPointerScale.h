#pragma once
#include <stdint.h>
struct SharedPointerScale {
  static constexpr unsigned defaultSpeed=100,minSpeed=1,maxSpeed=1600;
  static bool valid(unsigned value){return value>=minSpeed&&value<=maxSpeed;}
  static int gain(unsigned speed,unsigned adjustment=100){return int(int64_t(32767)*speed*adjustment/100);}
  static int denominator(unsigned pixels){return int(100*pixels);}
  static unsigned estimate(unsigned width,unsigned height,unsigned spanX,unsigned spanY,unsigned adjustment=100){
    if(!spanX||!spanY)return defaultSpeed;
    const uint64_t n=uint64_t(width)*spanY+uint64_t(height)*spanX;
    const uint64_t d=2*uint64_t(spanX)*spanY*adjustment;
    const uint64_t value=(10000*n+d/2)/d;
    return value<minSpeed?minSpeed:value>maxSpeed?maxSpeed:unsigned(value);
  }
};
