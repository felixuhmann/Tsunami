// SPDX-License-Identifier: GPL-3.0-or-later
// Core meter tap for the shared CoreMeterSource bridge. The tap reads state the
// existing core already computes; it is a project-defined measurement, not a
// proven reproduction of the original meter scale or ballistics.
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>
using tsunami::rvox::Processor;
constexpr double pi=3.14159265358979323846; // MSVC has no M_PI by default
static void check(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}

static_assert(Processor::meterCount==2,"energy + total gain");
static_assert(std::is_same_v<decltype(std::declval<const Processor&>().meter(0u)),float>,"bridge signature");
static_assert(noexcept(std::declval<const Processor&>().meter(0u)),"bridge getter is noexcept");

// Feeds a sine of the given peak and returns the peak output.
static float run(Processor& p,unsigned ch,float amplitude,unsigned frames) {
 std::vector<float> x[2]{std::vector<float>(frames),std::vector<float>(frames)};
 std::vector<float> y[2]{std::vector<float>(frames),std::vector<float>(frames)};
 for(unsigned n=0;n<frames;++n) x[0][n]=x[1][n]=amplitude*float(std::sin(2*pi*440*n/48000.));
 const float* in[]={x[0].data(),x[1].data()}; float* out[]={y[0].data(),y[1].data()};
 float peak=0;
 for(unsigned n=0;n<frames;n+=256) {
  const unsigned len=std::min(256u,frames-n);
  const float* i2[]={in[0]+n,in[1]+n}; float* o2[]={out[0]+n,out[1]+n};
  p.process(i2,o2,len);
  for(unsigned c=0;c<ch;++c) for(unsigned k=0;k<len;++k) peak=std::max(peak,std::abs(o2[c][k]));
  for(unsigned m=0;m<Processor::meterCount;++m) check(std::isfinite(p.meter(m)),"meters stay finite per block");
 }
 return peak;
}

int main() {
 Processor p; p.prepare(48000,2,512);
 check(p.meter(Processor::kEnergy)==0.f,"silence after prepare: zero detector energy");
 check(p.meter(Processor::kTotalGain)==1.f,"no reduction after prepare: unity total gain");
 check(p.meter(7)==0.f,"out-of-range index reads 0");
 run(p,2,0.f,4800);
 check(p.meter(Processor::kEnergy)==0.f && std::abs(p.meter(Processor::kTotalGain)-1.f)<1e-6f,"silence keeps idle readings");
 std::puts("PASS idle/silence readings");

 // Default controls: unity-threshold law still applies peak/(peak*0+1) => no reduction.
 p.setParameter(0,-24); p.setParameter(1,-80); p.setParameter(2,0);
 const float outPeak=run(p,2,.5f,48000);
 const float energy=p.meter(Processor::kEnergy), total=p.meter(Processor::kTotalGain);
 check(energy>.45f && energy<=.75f,"energy tracks detector peak of the 0.5 input (allpass peaks may exceed)");
 const double t=std::pow(10.,-24./20.);
 check(total>0.f && total<.2f,"-24dB threshold on 0.5 sine reduces gain by >14dB");
 check(std::abs(total-t/(energy*(1-t)+t))<.05,"total gain is the core's rational law at the detector level");
 check(std::abs(outPeak-energy)>.05f,"input energy is not the output level");
 std::puts("PASS compression reading under -24dB threshold");

 p.reset();
 check(p.meter(Processor::kEnergy)==0.f && std::abs(p.meter(Processor::kTotalGain)-1.f)<1e-6f,"reset clears readings");
 p.setParameter(0,0); p.setParameter(1,-20);
 run(p,2,.001f,9600); // -60dBFS, far below the -20dB gate
 check(p.meter(Processor::kTotalGain)<.05f,"gate closes total gain for quiet input");
 run(p,2,.5f,9600);
 check(p.meter(Processor::kTotalGain)>.9f,"gate opens for loud input at unity threshold");
 std::puts("PASS gating reading");

 Processor m; m.prepare(48000,1,512); m.setParameter(0,-12);
 run(m,1,1.f,24000);
 check(m.meter(Processor::kEnergy)>.9f && m.meter(Processor::kTotalGain)<1.f,"mono core taps too");
 std::puts("PASS mono tap");
}
