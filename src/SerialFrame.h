#pragma once
#include <stddef.h>
#include <stdint.h>
// @ starts a bounded request. Discard the entire bad frame, including digits
// that would otherwise be legacy slot-selection commands.
class SerialFrame {
  char buffer[2049]{};size_t used=0;bool active=false,overflow=false;uint32_t started=0;
public:
  enum Result {Legacy,Consumed,Ready,Invalid};
  const char *data()const{return buffer;}
  Result feed(char c,uint32_t now){
    if(!active){if(c!='@')return Legacy;active=true;overflow=false;used=0;started=now;return Consumed;}
    if(uint32_t(now-started)>3000)overflow=true;
    if(c=='\n'){active=false;buffer[used]=0;return overflow?Invalid:Ready;}
    if(c=='\r')return Consumed;
    if(used==sizeof(buffer)-1)overflow=true;
    if(!overflow)buffer[used++]=c;
    return Consumed;
  }
};
