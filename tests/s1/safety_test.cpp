// SPDX-License-Identifier: GPL-3.0-or-later
// Additional regression coverage for the preserved implementation, not a claim
// that its interrupted development had witnessed RED-before-GREEN history.
#include "Processor.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t n) { ++allocations; if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
static void require(bool ok,const char* what) { if(!ok){std::cerr<<what<<'\n';std::exit(1);} }
int main() {
 constexpr unsigned frames=8192;
 std::vector<float> l(frames),r(frames),a(frames),b(frames),c(frames),d(frames);
 for(unsigned n=0;n<frames;++n){l[n]=.2f*std::sin(float(n)*.13f);r[n]=.15f*std::cos(float(n)*.07f);}
 unsigned layouts=0;double maxError=0;
 for(double rate:{44100.,48000.,96000.,192000.}) for(unsigned block:{1u,64u,512u,4096u}) {
  tsunami::s1::Processor whole,chunked;
  whole.prepare(rate,2,frames);chunked.prepare(rate,2,block);
  for(auto* p:{&whole,&chunked}) {p->setParameter(0,-3);p->setParameter(1,2.4f);p->setParameter(2,-17);p->setParameter(3,27);}
  const float* input[]{l.data(),r.data()};float* output[]{a.data(),b.data()};
  unsigned before=allocations.load();whole.process(input,output,frames);
  require(allocations.load()==before,"whole process allocated");
  for(unsigned pos=0;pos<frames;pos+=block){const float* in[]{l.data()+pos,r.data()+pos};float* out[]{c.data()+pos,d.data()+pos};chunked.process(in,out,std::min(block,frames-pos));}
  require(allocations.load()==before,"chunked process allocated");
  for(unsigned n=0;n<frames;++n) {double e=std::max(std::abs(double(a[n])-c[n]),std::abs(double(b[n])-d[n]));maxError=std::max(maxError,e);require(e<=1e-6,"block partition mismatch");require(std::isfinite(a[n])&&std::isfinite(b[n]),"nonfinite output");}
  c=l;d=r;whole.reset();chunked.reset();whole.process(input,output,frames);
  const float* inplaceIn[]{c.data(),d.data()};float* inplaceOut[]{c.data(),d.data()};chunked.process(inplaceIn,inplaceOut,frames);
  for(unsigned n=0;n<frames;++n)require(a[n]==c[n]&&b[n]==d[n],"in-place differs");
  std::fill(c.begin(),c.end(),0);std::fill(d.begin(),d.end(),0);chunked.process(inplaceIn,inplaceOut,frames);
  for(unsigned n=0;n<frames;++n)require(c[n]==0&&d[n]==0,"matrix produces tail from silence");
  ++layouts;
 }
 tsunami::s1::Processor controls;controls.prepare(48000,2,512);
 constexpr std::array<float,9> lows{{-24,0,-45,-90,0,0,0,0,0}},highs{{0,3,45,90,1,1,1,1,1}};
 unsigned before=allocations.load();
 for(unsigned i=0;i<9;++i){controls.setParameter(i,-1e9f);require(controls.getParameter(i)==lows[i],"lower clamp");controls.setParameter(i,1e9f);require(controls.getParameter(i)==highs[i],"upper clamp");controls.setParameter(i,NAN);require(controls.getParameter(i)==highs[i],"NaN changed parameter");controls.setParameter(i,INFINITY);require(controls.getParameter(i)==highs[i],"infinity changed parameter");}
 controls.setParameter(999,0);controls.process(nullptr,nullptr,0);require(controls.getParameter(999)==0,"out-of-range getter");
 require(allocations.load()==before,"setParameter or zero-frame processing allocated");
 std::cout<<"safety: "<<layouts<<" rate/block cases; max partition error="<<maxError<<"; inplace exact; finite; silence exact; controls bounded; process/control allocations=0\n";
}
