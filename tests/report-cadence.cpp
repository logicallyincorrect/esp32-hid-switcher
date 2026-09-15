#include "ReportCadence.h"
#include <cassert>
int main(){
 ReportCadence c;
 assert(c.due(1000,15000));assert(!c.due(15999,15000));
 assert(c.due(16500,15000));assert(!c.due(16500,15000));
 assert(c.due(31000,15000)); // 500us lateness must not shift the schedule
 assert(c.due(101000,15000));assert(!c.due(101001,15000));
 assert(c.due(106000,15000)); // skip missed frames without a catch-up burst
 ReportCadence wrap;assert(wrap.due(0xfffffff0,15000));
 assert(!wrap.due(100,15000));assert(wrap.due(14984,15000));
}
