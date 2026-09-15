#include "ReconnectGuard.h"
#include <cassert>
int main(){
 using A=ReconnectGuard::Action;ReconnectGuard a,b;
 a.reset(10);b.reset(10);
 assert(a.poll(10,false,false)==A::None);assert(b.poll(10,false,false)==A::None);
 assert(a.poll(500,false,false)==A::None);assert(a.poll(1010,false,false)==A::Secure);
 assert(a.poll(1200,true,false)==A::None);
 assert(a.poll(6200,true,false)==A::Refresh);
 assert(a.poll(16199,true,false)==A::None);assert(a.poll(16200,true,false)==A::Disconnect);
 assert(a.poll(17000,true,false)==A::None);
 assert(b.poll(1000,true,true)==A::None);assert(b.poll(600000,true,true)==A::None);
 b.reset(100);assert(b.poll(1100,false,false)==A::Secure);b.securityAccepted=true;assert(b.poll(2000,false,false)==A::None);
 a.reset(100);assert(a.poll(45100,false,false)==A::Disconnect);
 a.reset(0xfffffff0u);assert(a.poll(0xfffffff0u,false,false)==A::None);
 assert(a.poll(984,false,false)==A::Secure);
 a.reset(5);assert(a.poll(6,true,true)==A::None);assert(a.poll(60000,true,true)==A::None);
}
