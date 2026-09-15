#include "TimingStats.h"
#include "MousePending.h"
#include <cassert>
int main(){
 TimingStats s;assert(s.count==0&&s.mean()==0);
 s.add(1000);s.add(3000);assert(s.min==1000&&s.max==3000&&s.mean()==2000&&s.deviation()==1000&&s.p95Upper()==4000);
 ReportSpacing r;r.observe(0xfffffff0);r.observe(984);assert(r.samples.count==1&&r.samples.mean()==1000);
 r.observe(200984);assert(r.idleGaps==1&&r.samples.count==1);r.observe(208984);assert(r.samples.count==2);
 MousePending q;MouseReport m;q.push({0,2,0,0,0},100);q.push({0,3,0,0,0},200);
 assert(q.oldest()==100&&q.newest()==200&&q.peek(m)&&m.x==5);
 // A failed submission retains both timestamps. A button edge starts a new group.
 q.push({1,1,0,0,0},300);assert(q.oldest()==100&&q.newest()==200);q.accepted(m);
 assert(q.oldest()==300&&q.newest()==300);q.clear();assert(q.oldest()==0);
}
