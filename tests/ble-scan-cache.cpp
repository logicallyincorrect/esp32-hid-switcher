#include "BleScanCache.h"
#include <cassert>
int main(){
  BleScanCache cache;uint8_t address[6]={1,2,3,4,5,6};
  const uint8_t response[]={6,9,'P','h','o','n','e'};
  const uint8_t advertising[]={3,3,0x12,0x18};
  cache.observe(address,1,false,response,sizeof(response));
  assert(cache.count==1&&!cache.entries[0].connectable);
  cache.observe(address,1,true,advertising,sizeof(advertising));
  assert(cache.count==1&&cache.entries[0].connectable&&cache.entries[0].hid);
  assert(!strcmp(cache.entries[0].name,"Phone"));
  const uint8_t shortName[]={2,8,'P'};
  cache.observe(address,1,false,shortName,sizeof(shortName));
  assert(!strcmp(cache.entries[0].name,"Phone")&&cache.entries[0].connectable);
  const uint8_t malformed[]={30,9,'X'};cache.observe(address,1,true,malformed,sizeof(malformed));
  assert(!strcmp(cache.entries[0].name,"Phone"));
  cache.observe(address,0,true,nullptr,0);assert(cache.count==2); // address type is part of identity
  for(unsigned i=0;i<100;++i){address[0]=i;cache.observe(address,2,true,nullptr,0);}
  assert(cache.count==96&&cache.limited);
  cache.clear();cache.observe(address,2,true,nullptr,0);
  assert(cache.count==1&&!cache.limited&&!cache.entries[0].hid&&!cache.entries[0].name[0]);
  const uint8_t uuid128[]={17,7,0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x12,0x18,0,0};
  cache.observe(address,2,true,uuid128,sizeof(uuid128));assert(cache.entries[0].hid);
}
