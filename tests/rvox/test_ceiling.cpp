// SPDX-License-Identifier: GPL-3.0-or-later
// Output ceiling (Gain) behaviour of the RVox core, round-2 task3.
// Characterization of preserved code, checked against a genuine render; NOT TDD
// provenance for new behaviour (no production change was needed).
//  1. Fixed controls, input <= 0 dBFS: output peak never exceeds 0.99*Gain.
//  2. Fixed controls, +20 dBFS sine (amplitude 10) at Comp -6, Gain -28.7: the
//     genuine RVox Stereo output peak is 0.06624011 (-23.58 dBFS, 5.1 dB ABOVE
//     the Gain value; oracle evidence 20261027T162705-7031497693d94ed2befb45aaeba92a4a,
//     whole-file core vs genuine max_abs 1.49e-8). Gain is a ceiling for <=0 dBFS
//     input only; hot input legitimately exceeds it, by design of the recovered law.
//  3. A Compression decrease takes effect in makeup immediately while the gain
//     smoother still holds the old reduction, so a step 0 -> -36 dB overshoots the
//     ceiling by up to ~36 dB for a few ms. Reported only: the oracle renders
//     static controls, so whether the original does the same is NOT verified.
#include "Processor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static void check(bool value, const char* reason) {
 if(!value) {std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}
}
constexpr double pi=3.14159265358979323846;
using Signal=std::vector<float>;
// Stereo render with per-block controls; returns output peak over all samples.
template<class Controls>
static float render(const Signal& l,const Signal& r,unsigned block,Controls controls) {
 tsunami::rvox::Processor p; p.prepare(48000,2,block);
 Signal ol(l.size()),orr(r.size()); float peak=0;
 for(size_t at=0;at<l.size();at+=block) {
  const unsigned n=unsigned(std::min<size_t>(block,l.size()-at));
  controls(p,at);
  const float* in[]={l.data()+at,r.data()+at}; float* out[]={ol.data()+at,orr.data()+at};
  p.process(in,out,n);
 }
 for(size_t i=0;i<l.size();++i) {check(std::isfinite(ol[i])&&std::isfinite(orr[i]),"finite");peak=std::max({peak,std::abs(ol[i]),std::abs(orr[i])});}
 return peak;
}
int main() {
 const float gain=-28.7f; const double ceiling=.99*std::pow(10.,gain/20.);
 std::mt19937 rng(17); std::uniform_real_distribution<float> u(-1.f,1.f);
 Signal nl(48000),nr(48000),sl(24000),sr(24000);
 for(auto* s:{&nl,&nr}) for(float& v:*s) v=u(rng);
 for(unsigned n=0;n<24000;++n) {
  sl[n]=float(10*std::sin(2*pi*997*n/48000)); sr[n]=float(5*std::sin(2*pi*997*n/48000+1.0));
 }
 // 1. <=0 dBFS input, fixed controls: ceiling holds.
 for(float comp:{0.f,-0.1f,-6.f,-18.f,-36.f}) {
  const float peak=render(nl,nr,512,[&](tsunami::rvox::Processor& p,size_t at){if(!at){p.setParameter(0,comp);p.setParameter(2,gain);}});
  std::printf("noise <=0dBFS comp %6.1f: peak %.2f dBFS (ceiling %.2f)\n",comp,20*std::log10(peak),20*std::log10(ceiling));
  check(peak<=ceiling*(1+1e-5),"<=0 dBFS input exceeds Gain ceiling at fixed controls");
 }
 // 2. +20 dBFS sine, genuine-confirmed overshoot of the Gain value.
 const float hot=render(sl,sr,512,[&](tsunami::rvox::Processor& p,size_t at){if(!at){p.setParameter(0,-6);p.setParameter(1,-80);p.setParameter(2,gain);}});
 std::printf("+20 dBFS sine comp -6: peak %.8f (%.2f dBFS), genuine 0.06624011\n",hot,20*std::log10(hot));
 check(std::abs(hot-0.0662401095f)<=1e-6f,"hot-input peak differs from genuine render");
 check(hot>ceiling,"hot input should exceed Gain value as the genuine original does");
 // 3. Compression step at <=0 dBFS input: transient overshoot (not verified vs genuine).
 const float step=render(nl,nr,512,[&](tsunami::rvox::Processor& p,size_t at){
  if(!at){p.setParameter(0,0);p.setParameter(2,gain);} if(at==24064)p.setParameter(0,-36);});
 std::printf("comp step 0->-36 at <=0dBFS: transient peak %.2f dBFS (unverified vs genuine)\n",20*std::log10(step));
 std::printf("rvox.ceiling PASS\n");
 return 0;
}
