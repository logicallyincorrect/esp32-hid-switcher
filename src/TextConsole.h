#pragma once
#include <stdint.h>
#include <string>

struct TextKey { uint8_t code=0,modifiers=0; };
// US keyboard layout. The host interprets these HID usages; Caps Lock must be off.
inline TextKey textKey(char c){
  if(c>='a'&&c<='z')return {uint8_t(4+c-'a'),0};
  if(c>='A'&&c<='Z')return {uint8_t(4+c-'A'),2};
  if(c>='1'&&c<='9')return {uint8_t(30+c-'1'),0};
  if(c=='0')return {39,0};
  if(c=='\n')return {40,0};
  if(c=='\b')return {42,0};
  if(c==' ')return {44,0};
  const char plain[]="-=[]\\;\x27`,./";
  const char shifted[]="_+{}|:\"~<>?";
  const uint8_t codes[]={45,46,47,48,49,51,52,53,54,55,56};
  for(unsigned i=0;i<sizeof(codes);++i){if(c==plain[i])return {codes[i],0};if(c==shifted[i])return {codes[i],2};}
  const char digits[]="!@#$%^&*()";
  for(unsigned i=0;i<10;++i)if(c==digits[i])return {uint8_t(30+i),2};
  return {56,2}; // Non-ASCII display fallback; stored names remain unchanged.
}
inline char inputCharacter(uint8_t key,uint8_t modifiers){
  if(modifiers&~0x22)return 0;
  const bool shifted=modifiers&0x22;
  for(unsigned c=32;c<127;++c){const auto k=textKey(char(c));if(k.code==key&&bool(k.modifiers)==shifted)return char(c);}
  return 0;
}
class TextConsole {
  std::string _text;
  size_t _position=0;
  bool _release=false,_paced=false;
  uint32_t _last=0;
public:
  bool busy()const{return _position<_text.size()||_release;}
  void clear(){_text.clear();_position=0;_release=false;_paced=false;}
  bool append(const std::string &text){
    if(!busy())clear();
    if(_text.size()+text.size()>4096)return false;
    _text+=text;return true;
  }
  template<class Sender> void tick(uint32_t now,uint32_t spacing,Sender send){
    if(!busy()||(_paced&&uint32_t(now-_last)<spacing))return;
    uint8_t report[8]={};
    if(!_release){const auto key=textKey(_text[_position]);report[0]=key.modifiers;report[2]=key.code;}
    if(!send(report))return; // Retry the same press/release under backpressure.
    _last=now;_paced=true;
    if(_release){++_position;_release=false;if(!busy())clear();}else _release=true;
  }
};
class MenuButton {
  uint32_t _started=0;
  bool _down=false,_handled=false;
public:
  bool update(bool down,bool active,uint32_t now){
    if(!down){_down=_handled=false;return false;}
    if(!_down){_down=true;_started=now;if(active){_handled=true;return true;}}
    if(!_handled&&uint32_t(now-_started)>=3000){_handled=true;return true;}
    return false;
  }
};
