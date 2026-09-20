#include "CalibrationFeedback.h"
#include <cassert>
#include <vector>
#include <array>
int main(){
  CalibrationFeedback feedback;std::vector<std::array<uint8_t,8>> sent;
  auto send=[&](const uint8_t *r){std::array<uint8_t,8> packet;for(unsigned i=0;i<8;++i)packet[i]=r[i];sent.push_back(packet);return true;};
  assert(feedback.begin("OK",1,8,0));
  auto fail=[](const uint8_t *){return false;};
  assert(feedback.tick(0,15,1,8,true,fail)==CalibrationFeedback::Waiting);
  assert(feedback.tick(1,15,1,8,true,send)==CalibrationFeedback::Waiting);
  assert(feedback.tick(16,15,1,8,true,send)==CalibrationFeedback::Waiting);
  assert(feedback.tick(31,15,1,8,true,send)==CalibrationFeedback::Waiting);
  assert(sent.back()[2]!=0); // cannot switch hosts while final letter is held
  assert(feedback.tick(46,15,1,8,true,send)==CalibrationFeedback::Done);assert(sent.back()[2]==0);
  auto count=sent.size();feedback.begin("Success",1,8,100);
  assert(feedback.tick(101,15,2,8,true,send)==CalibrationFeedback::LostHost);assert(sent.size()==count);
  feedback.begin("Success",1,8,100);assert(feedback.tick(101,15,1,9,true,send)==CalibrationFeedback::LostHost);
  feedback.begin("Success",1,8,100);assert(feedback.tick(101,15,1,8,false,send)==CalibrationFeedback::LostHost);
  feedback.begin("Success",1,8,100);assert(feedback.tick(120101,15,1,8,true,send)==CalibrationFeedback::LostHost);
}
