#include "AbsolutePointer.h"
#include <cassert>
int main(){
  MonitorLayout::Legacy old{1,7,1,{{0,0,3440,1440,1,0},{3440,180,1920,1080,1,0},{5360,0,1920,1080,0,1}}};
  MonitorLayout migrated;assert(migrated.load(&old,sizeof(old)));
  assert(migrated.version==2&&migrated.generation==7&&migrated.active==1);
  assert(migrated.screens[1].x==3440&&migrated.screens[1].y==180&&migrated.screens[0].width==3440);
  for(auto &screen:migrated.screens)assert(screen.sensitivity==100);
  migrated.screens[1].sensitivity=125;MonitorLayout restored;assert(restored.load(&migrated,sizeof(migrated)));
  assert(restored.screens[1].sensitivity==125);
  migrated.screens[1].sensitivity=401;assert(!restored.load(&migrated,sizeof(migrated)));
  assert(restored.screens[1].sensitivity==125);assert(!restored.load(&old,1));
  old.version=9;assert(!restored.load(&old,sizeof(old)));
  MonitorLayout l;l.active=1;assert(l.valid());int16_t x=-1,y=-1;
  l.screens[0]={0,0,3440,1440,1};l.screens[1]={3440,180,1920,1080,1};l.screens[2].enabled=0;
  assert(l.valid());
  assert(l.crossing(0,1,32767,16384,3,x,y)==1&&x==0&&y>=16383&&y<=16385);
  assert(l.crossing(0,1,32767,0,3,x,y)==-1); // above overlap
  assert(l.crossing(0,1,32767,32767,3,x,y)==-1); // below overlap
  assert(l.crossing(0,1,32767,16384,1,x,y)==-1); // offline target
  l.screens[2]={5360,0,1920,1080,1};
  assert(l.crossing(0,1,32767,16384,5,x,y)==2); // skip disconnected middle output
  l.screens[2].enabled=0;
  assert(l.crossing(1,-1,0,0,3,x,y)==0&&x==32767&&y==4095);
  l.screens[1].x++;assert(l.valid());assert(l.crossing(0,1,32767,16384,3,x,y)==-1); // real gap
  l.screens[1].x-=2;assert(!l.valid());
  l.screens[1]={760,-1080,1920,1080,1};assert(l.valid());
  assert(l.crossing(0,-2,16384,0,3,x,y)==1&&y==32767&&x>=16383&&x<=16385);
  assert(l.crossing(1,2,16384,32767,3,x,y)==0&&y==0);
  l.screens[1]={3440,-1080,1920,1080,1};assert(l.valid());
  assert(l.crossing(0,-2,32767,0,3,x,y)==-1); // corner-only contact
  l.screens[1]={0,-1080,1920,1080,1};l.screens[2]={1920,-1080,1920,1080,1};assert(l.valid());
  assert(l.crossing(0,-2,30000,0,7,x,y)==2); // choose neighbour under crossing point
  l.screens[2].enabled=0;l.screens[1].width=0;assert(!l.valid());
  l.screens[1].width=1920;l.screens[1].x=65537;assert(!l.valid());

  AbsolutePointer p;p.layout.active=1;p.layout.screens[0]={0,0,3440,1440,1};p.layout.screens[1]={760,-1080,1920,1080,1};p.layout.screens[2].enabled=0;
  p.sync(0,3,true,0);p.positions[0]={16384,1};MouseReport r{0,0,-1,0,0};
  assert(p.motion(r,false,0,1,1,1,1));assert(p.destination()==1);
  auto entry=p.enter(1,1);assert(entry.y==32767&&entry.x>=16383&&entry.x<=16385);
  r={0,0,1,0,0};assert(!p.motion(r,false,0,2,1,1,1)); // guard is vertical
  r={0,0,1,0,0};assert(p.motion(r,false,0,81,1,1,1));assert(p.destination()==0);
  p.enter(0,81);p.positions[0]={0,0};r={0,0,-100,0,0};assert(!p.motion(r,false,0,200,1,1,1)); // no shared edge here
  p.positions[0]={16384,0};r={1,0,-100,0,0};assert(!p.motion(r,false,0,201,1,1,1)); // drag
  r={0,0,-100,0,0};assert(!p.motion(r,true,0,202,1,1,1));
  r={0,0,-100,0,0};assert(!p.motion(r,false,101,203,1,1,1));
  r={0,0,-1,0,0};assert(!p.motion(r,false,0,204,3,1,1));
  r={0,0,-1,0,0};assert(!p.motion(r,false,0,205,3,1,1));
  r={0,0,-1,0,0};assert(p.motion(r,false,0,206,3,1,1));
  // A diagonal packet hits the top seam before the right seam.
  AbsolutePointer diagonal;diagonal.layout.active=1;
  diagonal.layout.screens[0]={0,0,1920,1080,1};diagonal.layout.screens[1]={1920,0,1920,1080,1};diagonal.layout.screens[2]={0,-1080,1920,1080,1};
  diagonal.sync(0,7,true,0);diagonal.positions[0]={32000,100};
  r={0,1000,-1000,0,0};assert(diagonal.motion(r,false,0,1,1,1,1));assert(diagonal.destination()==2);
  entry=diagonal.enter(2,1);assert(entry.x==32100&&entry.y==32767);
}
