// SPDX-License-Identifier: GPL-3.0-or-later
// Numeric-domain and control-precision regressions (review F1/F2 at 5389ab8).
#include "Processor.hpp"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using tsunami::rvox::Processor;
static void check(bool ok,const char* text) {if(!ok) {std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
using Buffer=std::vector<std::vector<float>>;
static Buffer run(Processor& p,const Buffer& x) {
 Buffer y(x.size(),std::vector<float>(x[0].size()));
 const float* in[]={x[0].data(),x.back().data()}; float* out[]={y[0].data(),y.back().data()};
 p.process(in,out,static_cast<unsigned>(x[0].size()));
 return y;
}
static Buffer signal(unsigned channels,unsigned frames) {
 Buffer x(channels,std::vector<float>(frames));
 for(unsigned c=0;c<channels;++c)for(unsigned n=0;n<frames;++n)x[c][n]=float(.1+.05*std::sin(n*.011*(c+1)));
 return x;
}
static void controls(Processor& p,float compression) {p.setParameter(0,compression);p.setParameter(1,-80);p.setParameter(2,0);}
static bool finite(const Buffer& y) {for(auto& c:y)for(float v:c)if(!std::isfinite(v))return false;return true;}
static void makeup() {
 // Recovered builder keeps dbToGain(compression) in double for the inverse
 // makeup and converts the product to float once.
 const struct {float compression,gain,expected;} cases[]={
  {-18,-3,11.112653732299805f},{-30,-9,22.172658920288086f},{-36,0,124.68602752685547f},{-6,0,3.942918300628662f}};
 Processor p; p.prepare(48000,1,512);
 for(auto& c:cases) {
  p.setParameter(0,c.compression);p.setParameter(2,c.gain);
  check(p.makeupCoefficient()==c.expected,"double-domain threshold for inverse makeup");
 }
 std::puts("PASS recovered double-domain makeup coefficient");
}
static void overload(unsigned channels,float compression) {
 constexpr unsigned frames=96000*2;
 const float values[]={FLT_MAX,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
  std::numeric_limits<float>::quiet_NaN()};
 for(float value:values) {
  Buffer x=signal(channels,frames),clean=x;
  for(unsigned c=0;c<channels;++c) {
   const float sign=c==0?1.f:-1.f;
   x[c][100]=sign*value; x[c][101]=-sign*value;
   clean[c][100]=clean[c][101]=0;
  }
  Processor p,fresh; p.prepare(48000,channels,512); fresh.prepare(48000,channels,512);
  controls(p,compression); controls(fresh,compression);
  const Buffer y=run(p,x), reference=run(fresh,clean);
  check(finite(y),"overload and nonfinite input keep every output finite");
  if(!std::isfinite(value)) {
   // Replacement-only policy: nonfinite input samples are treated as silence.
   for(unsigned c=0;c<channels;++c)
    check(std::memcmp(y[c].data(),reference[c].data(),frames*sizeof(float))==0,"nonfinite input equals zero substitution");
  } else {
   for(unsigned c=0;c<channels;++c) {
    check(y[c].back()>0,"continued positive signal keeps positive gain");
    check(std::abs(y[c].back()-reference[c].back())<=1e-6f*std::abs(reference[c].back()),"finite extreme recovers ordinary equations");
    if(compression==0)
     for(unsigned n=200;n<frames;++n)check(y[c][n]==reference[c][n],"unity threshold ignores finite overload in detector");
   }
  }
  p.reset(); fresh.reset();
  const Buffer after=run(p,clean), again=run(fresh,clean);
  for(unsigned c=0;c<channels;++c)
   check(std::memcmp(after[c].data(),again[c].data(),frames*sizeof(float))==0,"reset clears overload state");
 }
}
static void ordinary() {
 // Ordinary floating-point audio above 0dBFS is not clipped.
 Processor p; p.prepare(48000,1,512); controls(p,0);
 Buffer x(1,std::vector<float>(4096,8.f));
 const Buffer y=run(p,x);
 check(std::abs(y[0].back()-.99f*8.f)<1e-5f,"+18dBFS signal at unity threshold passes unclipped");
 std::puts("PASS >0dBFS ordinary domain unclipped");
}
static void hostRates() {
 // Hosts (clap-validator process-varying-sample-rates) may prepare at any positive rate;
 // a throw escaping prepareToPlay aborts the host. Outside 8-384kHz the core designs at the
 // nearest supported rate; inside the domain nothing changes.
 for(double rate:{1000.,4000.,768000.,1536000.})for(unsigned channels:{1u,2u}) {
  Processor p,edge;
  try {p.prepare(rate,channels,512);} catch(...) {check(false,"positive host rate outside 8-384kHz must not throw");}
  edge.prepare(rate<8000?8000.:384000.,channels,512);
  check(p.latencySamples()==edge.latencySamples(),"out-of-domain rate designs at nearest supported rate");
  controls(p,-18); controls(edge,-18);
  Buffer x(channels,std::vector<float>(4096));
  for(unsigned c=0;c<channels;++c)for(unsigned n=0;n<4096;++n)x[c][n]=float(.5*std::sin(.01*(n+1)*(c+1)));
  const Buffer y=run(p,x), z=run(edge,x);
  check(finite(y)&&y==z,"out-of-domain rate output finite and equal to edge design");
 }
 bool rejected=false;
 try {Processor p;p.prepare(std::numeric_limits<double>::quiet_NaN(),2,512);} catch(const std::invalid_argument&) {rejected=true;}
 check(rejected,"nonfinite rate still rejected");
 std::puts("PASS positive host rates outside 8-384kHz clamp to edge design");
}
int main(int argc,char** argv) {
 const char* only=argc>1?argv[1]:"";
 if(!*only||!std::strcmp(only,"makeup"))makeup();
 if(!*only||!std::strcmp(only,"rates"))hostRates();
 if(!*only||!std::strcmp(only,"overload")) {
  for(unsigned channels:{1u,2u})for(float compression:{0.f,-18.f})overload(channels,compression);
  std::puts("PASS finite-extreme/NaN/+Inf/-Inf continuation, reset, mono/stereo");
  ordinary();
 }
}
