#include "HidMouseParser.h"
#include "SlotConfig.h"
#include "MultiHostRouter.h"
#include <cassert>
#include <vector>
struct Sent {uint16_t handle;uint8_t id;uint8_t buttons;size_t length;};
static bool send(void *ctx,uint16_t h,const uint8_t *data,uint8_t id,size_t len){static_cast<std::vector<Sent>*>(ctx)->push_back({h,id,data[0],len});return true;}
int main(){
  // Common 3-button + wheel relative mouse; negative motion and padded buttons.
  const uint8_t descriptor[]={0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0};
  HidMouseParser parser;assert(parser.parse(descriptor,sizeof(descriptor)));
  MouseReport r;bool buttons=false;const uint8_t bytes[]={5,0xff,20,0xfe};
  assert(parser.decode(bytes,4,r,buttons)&&buttons&&r.buttons==5&&r.x==-1&&r.y==20&&r.wheel==-2);
  assert(!parser.decode(bytes,3,r,buttons));
  auto idDescriptor=std::vector<uint8_t>(descriptor,descriptor+sizeof(descriptor));idDescriptor.insert(idDescriptor.begin()+6,{0x85,7});
  assert(parser.parse(idDescriptor.data(),idDescriptor.size()));const uint8_t withId[]={7,5,0xff,20,0xfe};assert(parser.decode(withId,5,r,buttons)&&r.x==-1);assert(!parser.decode(bytes,4,r,buttons));
  for(size_t i=0;i<sizeof(descriptor);++i){HidMouseParser partial;assert(!partial.parse(descriptor,i));}
  auto absolute=std::vector<uint8_t>(descriptor,descriptor+sizeof(descriptor));absolute[absolute.size()-3]=2;assert(!parser.parse(absolute.data(),absolute.size()));
  SlotConfig original;original.selected=1;for(int i=0;i<3;++i){original.slots[i].assigned=1;original.slots[i].address[0]=10+i;}
  uint8_t order[]={2,0,1};char names[3][33]={"Desktop","Laptop","Work"};SlotConfig next;
  assert(reorderedConfig(original,order,names,next));assert(next.selected==2&&next.slots[0].address[0]==12&&next.slots[2].address[0]==11);
  uint8_t duplicate[]={0,0,2};assert(!reorderedConfig(original,duplicate,names,next));
  uint8_t invalid[]={0,1,3};assert(!validOrder(invalid));names[0][0]=0;assert(!reorderedConfig(original,order,names,next));
  std::vector<Sent> sent;MultiHostRouter router(send,&sent);
  for(int i=0;i<3;++i){router.connect(i,i+10);router.ready(i,true,3);}router.service();sent.clear();
  uint8_t mouse[7]={1,2,0,3,0,1,0};router.send(mouse,2,7);assert(sent.size()==1&&sent[0].id==2&&sent[0].handle==10);
  sent.clear();router.select(1);assert(sent.size()==4);for(auto &s:sent)assert(s.buttons==0);
  sent.clear();router.send(mouse,2,7);assert(sent.size()==1&&sent[0].handle==11);
  router.reorder(order);assert(router.selected()==2);sent.clear();router.send(mouse,2,7);assert(sent.size()==1&&sent[0].handle==11);
  router.ready(2,true,1);sent.clear();router.send(mouse,2,7);for(const auto &event:sent)assert(event.id!=2);
}
