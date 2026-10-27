// SPDX-License-Identifier: GPL-3.0-or-later
// Block-boundary parameter changes in the Tsunami core (no genuine automation oracle exists;
// thresholds predeclared in docs/evidence/deesser/thresholds.json "block_boundary_automation").
#include "Processor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using tsunami::deesser::Processor;
static void require(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
struct Result{double gainStep=0,clickExcess=0;};
// Renders 16 blocks of 512 at 48 kHz; applies (index,value) between block 8 and 9 and measures the 2048 samples after.
static Result run(bool wideband,unsigned index,float before,float after,float threshold){
 constexpr unsigned B=512,N=16*B,at=8*B; Processor p; p.prepare(48000,1,B);
 p.setParameter(1,threshold); p.setParameter(3,wideband?1.f:0.f); p.setParameter(index,before);
 std::vector<float> x(N),y(N); for(unsigned i=0;i<N;++i)x[i]=.5f*float(std::sin(2*3.14159265358979*7000.0*i/48000.0));
 for(unsigned b=0;b<N/B;++b){ if(b*B==at)p.setParameter(index,after);
  const float* in[]={x.data()+b*B}; float* out[]={y.data()+b*B}; p.process(in,out,B);}
 double slew=0; for(unsigned i=1;i<N;++i)slew=std::max(slew,double(std::abs(x[i]-x[i-1])));
 Result r; double prev=-1;
 for(unsigned i=at;i<at+2048;++i){
  r.clickExcess=std::max(r.clickExcess,double(std::abs(y[i]-y[i-1]))-slew);
  if(wideband&&std::abs(x[i])>=.1f){const double g=y[i]/x[i]; if(prev>=0&&i>at)r.gainStep=std::max(r.gainStep,std::abs(g-prev)); prev=g;} else prev=-1;
 }
 return r;
}
int main(){
 const double gainLimit=.05,clickLimit=.03;
 struct Case{const char* name;bool wide;unsigned idx;float a,b,thr;} cases[]={
  {"threshold 0->-40 wideband",true,1,0,-40,0},{"threshold -40->0 wideband",true,1,-40,0,0},
  {"threshold 0->-40 split",false,1,0,-40,0},{"threshold -40->0 split",false,1,-40,0,0},
  {"freq 4000->12000 wideband",true,0,4000,12000,-40},{"freq 12000->4000 wideband",true,0,12000,4000,-40},
  {"freq 4000->12000 split",false,0,4000,12000,-40},{"freq 12000->4000 split",false,0,12000,4000,-40}};
 bool ok=true;
 for(auto& c:cases){ // thresholds of a threshold-step case: the threshold under test is set by idx 1 itself
  const auto r=run(c.wide,c.idx,c.a,c.b,c.idx==1?c.a:c.thr);
  const bool pass=r.gainStep<=gainLimit&&r.clickExcess<=clickLimit;
  std::printf("%-28s max_gain_step=%.5f click_excess=%.5f %s\n",c.name,r.gainStep,r.clickExcess,pass?"PASS":"FAIL");
  ok=ok&&pass;
 }
 require(ok,"block-boundary parameter changes stay within predeclared click thresholds");
 // Realtime-safe attenuation meter (manual: "Atten: instantaneous gain reduction applied to the audio").
 Processor p; p.prepare(48000,1,512);
 require(p.gainReductionDb()==0.f,"meter reads 0 dB after prepare");
 std::vector<float> x(4096),y(4096); for(unsigned i=0;i<4096;++i)x[i]=.5f*float(std::sin(2*3.14159265358979*7000.0*i/48000.0));
 const float* in[]={x.data()}; float* out[]={y.data()};
 p.setParameter(1,-40); p.process(in,out,4096);
 require(p.gainReductionDb()>=3.f,"meter reports attenuation when compressing");
 p.reset(); require(p.gainReductionDb()==0.f,"reset clears the meter");
 p.setParameter(5,1); p.process(in,out,4096); require(p.gainReductionDb()==0.f,"bypass reports no attenuation");
 require(p.detectorLevelDb()<=0.f,"detector level meter is in dBFS");
 std::puts("PASS block-boundary automation and attenuation meter");
}
