#pragma once
#include <string>
namespace EdgeSettings {
constexpr unsigned defaultDistance=100, minDistance=1, maxDistance=10000;
inline bool valid(unsigned value){return value>=minDistance&&value<=maxDistance;}
inline bool parse(const std::string &text,unsigned &value){
  if(text.empty())return false;
  unsigned result=0;
  for(char c:text){
    if(c<'0'||c>'9')return false;
    result=result*10+unsigned(c-'0');
    if(result>maxDistance)return false;
  }
  if(!valid(result))return false;
  value=result;return true;
}
}
