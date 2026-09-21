#define HID_BLE_INPUT 1
#include "ControllerTracker.h"
#include <cassert>
int main(){ControllerTracker t;for(unsigned h=0;h<4;++h)t.submit(h,0,1);assert(!t.overflow);for(unsigned h=0;h<4;++h)t.complete(h,1,2);assert(!t.unmatched&&t.returned==4);}
