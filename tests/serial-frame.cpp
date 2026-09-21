#include "SerialFrame.h"
#include <cassert>
#include <string>
int main(){
  SerialFrame f;
  assert(f.feed('1',0)==SerialFrame::Legacy);
  assert(f.feed('@',0)==SerialFrame::Consumed);
  for(char c:std::string("HID1 {\"id\":1,\"op\":\"status\"}"))assert(f.feed(c,1)==SerialFrame::Consumed);
  assert(f.feed('\n',2)==SerialFrame::Ready);
  assert(std::string(f.data())=="HID1 {\"id\":1,\"op\":\"status\"}");
  f.feed('@',3);for(unsigned i=0;i<2100;++i)assert(f.feed('2',4)==SerialFrame::Consumed);
  assert(f.feed('\n',5)==SerialFrame::Invalid); // no digit escapes into legacy slot commands
  f.feed('@',10);assert(f.feed('3',3011)==SerialFrame::Consumed);assert(f.feed('\n',3012)==SerialFrame::Invalid);
  f.feed('@',4000);f.feed('x',4001);f.feed('\r',4001);assert(f.feed('\n',4002)==SerialFrame::Ready);
  assert(std::string(f.data())=="x");assert(f.feed('?',4003)==SerialFrame::Legacy);
}
