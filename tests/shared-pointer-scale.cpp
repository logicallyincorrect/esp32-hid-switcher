#include "AbsolutePointer.h"
#include "SharedPointerScale.h"
#include <cassert>
#include <cmath>
int main(){
  assert(SharedPointerScale::valid(100)&&!SharedPointerScale::valid(0)&&!SharedPointerScale::valid(1601));
  assert(SharedPointerScale::estimate(3440,1440,3440,1440)==100);
  assert(SharedPointerScale::estimate(1920,1080,3840,2160)==50);
  assert(SharedPointerScale::estimate(16384,16384,64,64)==1600);
  assert(SharedPointerScale::estimate(64,64,1000000,1000000)==1);
  assert(SharedPointerScale::gain(100,150)==SharedPointerScale::gain(150));
  assert(SharedPointerScale::gain(200,50)==SharedPointerScale::gain(100));
  assert(SharedPointerScale::estimate(1920,1080,1920,1080,200)==50);
  for(unsigned width:{1920u,3440u,3840u}){
    AbsolutePointer p;p.sync(0,1,true,0);p.positions[0]={0,0};
    MouseReport r{0,100,100,0,0};p.motion(r,false,0,1,1,SharedPointerScale::gain(100),SharedPointerScale::gain(100),SharedPointerScale::denominator(width),SharedPointerScale::denominator(1440));
    assert(std::abs(double(r.x)*width/32767-100)<0.12);
    assert(std::abs(double(r.y)*1440/32767-100)<0.05);
    // Dimensions change normalized travel, while desktop-pixel travel matches.
    p.positions[0]={0,0};p.reset(2);r={0,100,100,0,0};p.motion(r,false,0,3,1,SharedPointerScale::gain(200),SharedPointerScale::gain(200),SharedPointerScale::denominator(width),SharedPointerScale::denominator(1440));
    assert(std::abs(double(r.x)*width/32767-200)<0.12);
  }
}
