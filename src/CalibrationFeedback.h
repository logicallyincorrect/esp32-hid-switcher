#pragma once
#include "TextConsole.h"

// A calibration message belongs to the originating connection session. Drain
// its final key release before handing control to the next calibration slot.
class CalibrationFeedback {
  TextConsole text;
  unsigned host=0;
  uint32_t session=0,started=0;
  bool running=false;
public:
  enum Result { Waiting,Done,LostHost };
  bool active()const{return running;}
  void clear(){text.clear();running=false;}
  bool begin(const std::string &message,unsigned slot,uint32_t revision,uint32_t now){
    clear();host=slot;session=revision;started=now;running=text.append(message);return running;
  }
  template<class Sender> Result tick(uint32_t now,uint32_t spacing,unsigned selected,uint32_t revision,bool connected,Sender send){
    if(!running)return Done;
    if(!connected||selected!=host||revision!=session||uint32_t(now-started)>120000){clear();return LostHost;}
    text.tick(now,spacing,send);
    if(text.busy())return Waiting;
    running=false;return Done;
  }
};
