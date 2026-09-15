#pragma once
#include <stdint.h>
#include <math.h>
// Fixed-memory summaries. Histogram p95 is an upper bound, not an exact percentile.
struct TimingStats {
  static constexpr uint32_t bounds[9]={1000,2000,4000,8000,12000,16000,24000,50000,100000};
  uint32_t count=0,min=UINT32_MAX,max=0,bins[10]={};
  uint64_t sum=0,squares=0;
  void add(uint32_t us){++count;if(us<min)min=us;if(us>max)max=us;sum+=us;squares+=uint64_t(us)*us;unsigned i=0;while(i<9&&us>bounds[i])++i;++bins[i];}
  double mean()const{return count?double(sum)/count:0;}
  double deviation()const{if(!count)return 0;double v=double(squares)/count-mean()*mean();return sqrt(v>0?v:0);}
  uint32_t p95Upper()const{uint32_t n=0,target=(uint64_t(count)*95+99)/100;for(unsigned i=0;i<10;++i){n+=bins[i];if(n>=target)return i<9?bounds[i]:max;}return 0;}
};
struct ReportSpacing {
  TimingStats samples;
  uint32_t last=0,idleGaps=0;
  bool started=false;
  void observe(uint32_t now){if(started){const uint32_t gap=now-last;if(gap<=100000)samples.add(gap);else ++idleGaps;}last=now;started=true;}
};
