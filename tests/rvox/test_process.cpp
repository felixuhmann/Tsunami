// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

static void check(bool value,const char* reason) {
 if(!value) {std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}
}
using Buffer=std::array<std::vector<float>,2>;
static void render(tsunami::rvox::Processor& p,const Buffer& x,Buffer& y,unsigned frames,unsigned block,unsigned channels) {
 for(unsigned n=0;n<frames;n+=block) {
  const float* in[]={x[0].data()+n,x[1].data()+n};
  float* out[]={y[0].data()+n,y[1].data()+n};
  p.process(in,out,std::min(block,frames-n));
 }
 for(unsigned c=0;c<channels;++c)for(float value:y[c])check(std::isfinite(value),"finite render");
}
int main() {
 // Characterization of preserved code, NOT retroactive TDD provenance.
 for(double rate:{8000.,44100.,48000.,96000.,192000.})for(unsigned channels:{1u,2u}) {
  constexpr unsigned frames=12000;
  Buffer x,y,z;
  for(unsigned c=0;c<2;++c) {
   x[c].resize(frames);y[c].resize(frames);z[c].resize(frames);
   for(unsigned n=0;n<frames;++n)x[c][n]=float((n<3000?.2:.002)*std::sin((n+1)*(c+1)*.153));
  }
  tsunami::rvox::Processor p;p.prepare(rate,channels,511);
  p.setParameter(0,-18);p.setParameter(1,-20);p.setParameter(2,-3);
  render(p,x,y,frames,511,channels);p.reset();render(p,x,z,frames,1,channels);
  for(unsigned c=0;c<channels;++c)check(std::memcmp(y[c].data(),z[c].data(),frames*sizeof(float))==0,"reset/block partition bit-identical");
  p.reset();z=x;render(p,z,z,frames,127,channels);
  for(unsigned c=0;c<channels;++c)check(std::memcmp(y[c].data(),z[c].data(),frames*sizeof(float))==0,"in-place bit-identical");
  p.setParameter(0,std::numeric_limits<float>::quiet_NaN());check(p.getParameter(0)==-18,"nonfinite control ignored");
  p.setParameter(17,12);check(p.getParameter(17)==0,"unknown control ignored");
 }
 // Opposite-polarity duplicate channels must share the same detector/gain as
 // mono; the allpasses are detector-only, not inserted into the audio path.
 constexpr unsigned frames=12000;
 Buffer x,y,z;
 for(unsigned c=0;c<2;++c) {x[c].resize(frames);y[c].resize(frames);z[c].resize(frames);}
 for(unsigned n=0;n<frames;++n) {x[0][n]=float(.1*std::sin(.07*n));x[1][n]=-x[0][n];}
 tsunami::rvox::Processor mono,stereo;mono.prepare(48000,1,127);stereo.prepare(48000,2,127);
 for(auto* p:{&mono,&stereo}) {p->setParameter(0,-24);p->setParameter(1,-20);}
 render(mono,x,y,frames,127,1);render(stereo,x,z,frames,127,2);
 for(unsigned n=0;n<frames;++n) {check(y[0][n]==z[0][n],"stereo linked detector equals mono duplicate");check(z[1][n]==-z[0][n],"linked stereo preserves opposite polarity");}
 std::puts("PASS rate/channel/reset/block/in-place/control and stereo linked-chain characterization");
}
