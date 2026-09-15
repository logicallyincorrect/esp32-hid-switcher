#include "RecoveryPolicy.h"
#include <cassert>
int main(){
 RecoveryPolicy p;assert(!p.poll(100000));assert(!p.poll(999999));
 p.fault();assert(p.poll(1000000));assert(p.attempts==1);assert(!p.poll(1000001));
 p.fault();assert(!p.poll(1029999));assert(p.poll(1030000));assert(p.attempts==2);
 RecoveryPolicy wrap;wrap.fault();assert(wrap.poll(0xfffffff0));wrap.fault();assert(!wrap.poll(100));assert(wrap.poll(29984));
}
