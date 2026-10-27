// SPDX-License-Identifier: GPL-3.0-or-later
// Project-regression guard: rendered output of the portable core over stored
// deterministic inputs/controls/rates/channel counts/block sizes must stay
// bit-for-bit identical to the hashes captured from the core at ae4773d, before
// the meter tap was added. This is not a comparison against the original plugin.
#include "Processor.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using tsunami::rvox::Processor;

struct Case { double rate; unsigned channels; float threshold, gate, gain; };
static const Case cases[] = {
 {44100,1,0,-80,0},{44100,2,-18,-80,0},{48000,1,-24,-40,6},{48000,2,-36,-20,-6},
 {48000,2,-12,-60,3},{96000,1,-30,-30,0},{96000,2,-24,-80,-12},{192000,2,-6,-50,2},
};

static float source(unsigned c, uint64_t n, double rate, uint32_t& lcg) {
 lcg=lcg*1664525u+1013904223u;
 const double noise=(double(lcg>>8)/double(1u<<24))*2-1, t=double(n)/rate;
 const uint64_t part=uint64_t(t*4)%6; // tone, silence, hot, noise, swell, gated tail
 const double tone=std::sin(2*M_PI*(220.+110*c)*t);
 switch(part) {
  case 0: return float(.5*tone);
  case 1: return 0.f;
  case 2: return float(1.6*tone+.05*noise);
  case 3: return float(.3*noise);
  case 4: return float(std::fmod(t,.25)*3.2*tone);
  default: return float(.004*tone+.001*noise);
 }
}

static uint64_t render(const Case& k, Processor& p) {
 p.prepare(k.rate,k.channels,512);
 p.setParameter(0,k.threshold); p.setParameter(1,k.gate); p.setParameter(2,k.gain);
 const uint64_t frames=uint64_t(k.rate*3.1);
 static const unsigned blocks[]={1,37,512,64,7,300};
 uint32_t lcg=12345; uint64_t hash=1469598103934665603ull, n=0;
 std::vector<float> x[2],y[2];
 for(unsigned b=0;n<frames;++b) {
  const unsigned len=unsigned(std::min<uint64_t>(blocks[b%6],frames-n));
  for(unsigned c=0;c<2;++c){x[c].assign(len,0);y[c].assign(len,0);}
  for(unsigned i=0;i<len;++i) for(unsigned c=0;c<k.channels;++c) x[c][i]=source(c,n+i,k.rate,lcg);
  const float* in[]={x[0].data(),x[1].data()}; float* out[]={y[0].data(),y[1].data()};
  p.process(in,out,len);
  for(unsigned i=0;i<len;++i) for(unsigned c=0;c<k.channels;++c) {
   uint32_t bits; std::memcpy(&bits,&y[c][i],4);
   for(unsigned s=0;s<32;s+=8){hash^=(bits>>s)&255u;hash*=1099511628211ull;}
  }
  n+=len;
 }
 return hash;
}

// Captured from the unmodified ae4773d core (see docs/evidence/rvox/meter-tap.md).
static const uint64_t golden[] = {
 0x63eca1317a88d48full,
 0xe95378dcc919ed2dull,
 0x1d362752396c0c11ull,
 0x73acadcbb47ba791ull,
 0x14550666625f001bull,
 0x349571562529e826ull,
 0xe2a3a171a47231fdull,
 0x135b127d914b40f2ull,
};

int main(int argc,char**argv) {
 const bool print=argc>1 && std::strcmp(argv[1],"--print")==0;
 int failures=0; unsigned i=0;
 for(const auto& k:cases) {
  Processor p; const uint64_t h=render(k,p);
  if(print) std::printf(" 0x%016llxull,\n",static_cast<unsigned long long>(h));
  else if(i>=sizeof(golden)/sizeof(golden[0]) || golden[i]!=h) {
   std::fprintf(stderr,"FAIL case %u (%g Hz, %u ch): 0x%016llx\n",i,k.rate,k.channels,static_cast<unsigned long long>(h));
   ++failures;
  }
  ++i;
 }
 if(!print && !failures) std::puts("PASS output bit-identical to pre-meter core over 8 stored cases");
 return failures?1:0;
}
