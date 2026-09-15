#include "MousePending.h"
#include <cassert>
int main() {
  MousePending q;MouseReport r;
  assert(q.push({}));assert(!q.peek(r));
  for(int i=0;i<1000;++i)assert(q.push({0,2,-3,1,0}));
  assert(q.peek(r)&&r.x==2000&&r.y==-3000&&r.wheel==127);
  MouseReport retry;assert(q.peek(retry)&&retry.x==r.x); // failed send retains movement
  int wheels=0,x=0;while(q.peek(r)){wheels+=r.wheel;x+=r.x;q.accepted(r);}
  assert(wheels==1000&&x==2000);
  assert(q.push({1,5,0,0,0}));assert(q.push({0,7,0,0,0}));
  assert(q.peek(r)&&r.buttons==1&&r.x==5);q.accepted(r);
  assert(q.peek(r)&&r.buttons==0&&r.x==7);q.accepted(r);
  for(int i=0;i<32;++i)assert(q.push({uint8_t((i+1)%2),0,0,0,0}));
  assert(!q.push({1,0,0,0,0}));q.clear();assert(!q.peek(r));
  for(int i=0;i<3;++i)assert(q.push({0,30000,0,0,0}));
  x=0;while(q.peek(r)){x+=r.x;q.accepted(r);}assert(x==90000);
}
