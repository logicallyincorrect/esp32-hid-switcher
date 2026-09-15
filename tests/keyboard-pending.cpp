#include "KeyboardPending.h"
#include <cassert>
int main(){
 KeyboardPending q;uint8_t down[8]={0,0,4},up[8]={};
 assert(q.push(up)&&!q.peek());
 assert(q.push(down));assert(q.push(down));assert(q.push(up));
 assert(q.peek()[2]==4);assert(q.peek()[2]==4); // unsuccessful transport leaves down intact
 q.accepted();assert(q.peek()[2]==0);q.accepted();assert(!q.peek());
 for(int i=0;i<64;++i)assert(q.push(i%2?up:down));
 assert(!q.push(down));q.clear();assert(!q.peek());
 assert(q.push(down));q.accepted();assert(q.push(down)&&!q.peek());
 q.clear();assert(q.push(down)&&q.peek()); // new host must receive its own state
}
