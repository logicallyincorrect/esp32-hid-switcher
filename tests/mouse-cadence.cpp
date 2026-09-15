#include "MouseCadence.h"
#include "MousePending.h"
#include <cassert>
int main(){
 MouseCadence c;MousePending q;MouseReport r;
 q.push({0,1,0,0,0});assert(c.due(100,15000));
 assert(q.peek(r));q.accepted(r);c.accepted(100);
 q.push({0,2,0,0,0});q.push({0,3,0,0,0});
 assert(!c.due(15099,15000));assert(c.due(15100,15000));
 assert(q.peek(r)&&r.x==5);
 // A failed submission retains movement and does not consume the deadline.
 assert(c.due(16100,15000));assert(q.peek(r)&&r.x==5);
 q.accepted(r);c.accepted(16100);
 assert(!c.due(16101,15000));assert(!c.due(30099,15000));
 assert(c.due(30100,15000)); // one ms lateness did not move the next deadline
 c.accepted(30100);
 // Host interval changes anchor to the latest accepted submission.
 assert(!c.due(41349,11250));assert(c.due(41350,11250));c.accepted(41350);
 // Large stalls/idle restart; no immediate catch-up report.
 assert(c.due(1000000,11250));c.accepted(1000000);
 assert(!c.due(1000001,11250));assert(c.due(1011250,11250));
 MouseCadence wrap;assert(wrap.due(0xfffffff0,15000));wrap.accepted(0xfffffff0);
 assert(!wrap.due(100,15000));assert(wrap.due(14984,15000));wrap.accepted(15984);
 assert(!wrap.due(15985,15000));assert(wrap.due(29984,15000));
 // Repeated 1ms loop lateness must not accumulate to 16ms intervals.
 MouseCadence steady;assert(steady.due(0,15000));steady.accepted(0);
 for(uint32_t n=1;n<=1000;++n){const uint32_t now=n*15000+1000;
   assert(steady.due(now,15000));steady.accepted(now);assert(!steady.due(now+1,15000));}
 // Idle beyond the signed timer half-range must still resume immediately.
 assert(steady.due(0x90000000,15000));steady.accepted(0x90000000);
 assert(!steady.due(0x90000001,15000));
 q.push({1,2,0,0,0});q.push({1,3,0,0,0});q.push({0,4,0,0,0});
 assert(q.peek(r)&&r.buttons==1&&r.x==5);q.accepted(r);
 assert(q.peek(r)&&r.buttons==0&&r.x==4);
}
