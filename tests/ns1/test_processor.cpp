// SPDX-License-Identifier: GPL-3.0-or-later
// Contract test for the plugin-facing NS1 mono Processor.
#include "Processor.hpp"
#include "design.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace tsunami::ns1;
#define CHECK(c) do{if(!(c)){std::cerr<<"FAIL line "<<__LINE__<<": "#c"\n";return 1;}}while(0)
static std::vector<float> noise(unsigned n){std::vector<float> v(n);unsigned s=12345;for(auto& x:v){s=s*1664525u+1013904223u;x=(float(s>>8)/16777216.f-.5f)*.4f;}return v;}
int main(){
 // Parameter metadata mirrors the reference readback: NS1 0..100 % (default 0), Bypass Off.
 CHECK(Processor::ParameterCount==2);
 Processor p;p.prepare(48000,1,512);
 CHECK(p.getParameter(Processor::Amount)==0.f);
 CHECK(p.getParameter(Processor::Bypass)==0.f);
 CHECK(p.latencySamples()==0);
 CHECK(p.tailSeconds()>0);
 p.setParameter(Processor::Amount,250.f);CHECK(p.getParameter(Processor::Amount)==100.f);
 p.setParameter(Processor::Amount,-3.f);CHECK(p.getParameter(Processor::Amount)==0.f);
 p.setParameter(Processor::Amount,std::nanf(""));CHECK(p.getParameter(Processor::Amount)==0.f);
 // Stereo/linked processing is not implemented: refuse instead of running two independent monos.
 bool refused=false;try{Processor q;q.prepare(48000,2,512);}catch(const std::invalid_argument&){refused=true;}
 CHECK(refused);
 // Full chain equals the recovered mono chain (design + initial state) at sampled controls.
 const auto in=noise(4096);
 const float pct[]={0.f,50.f,100.f},norm[]={0.f,.5f,1.f};
 for(unsigned k=0;k<3;++k){
  Processor a;a.prepare(48000,1,512);a.setParameter(Processor::Amount,pct[k]);
  CHECK(a.controlEstablished());
  std::vector<float> buf=in;float* ch[]={buf.data()};
  for(unsigned off=0;off<buf.size();off+=500){float* c2[]={buf.data()+off};const float* i2[]={buf.data()+off};unsigned n=unsigned(std::min<std::size_t>(500,buf.size()-off));a.process(i2,c2,n);}
  (void)ch;
  Mono m(designSampledMono(48000,norm[k]),initialMonoState());
  bool nonTrivial=false;
  for(unsigned i=0;i<in.size();++i){const float e=m.process(in[i]);CHECK(buf[i]==e);nonTrivial|=buf[i]!=in[i];}
  CHECK(nonTrivial);
  // reset() restores the documented initial state.
  a.reset();std::vector<float> again=in;float* o[]={again.data()};const float* ii[]={in.data()};a.process(ii,o,unsigned(in.size()));
  Mono m2(designSampledMono(48000,norm[k]),initialMonoState());
  for(unsigned i=0;i<in.size();++i)CHECK(again[i]==m2.process(in[i]));
 }
 // Off-grid controls have no established mapping: flagged, never silently claimed.
 {Processor a;a.prepare(48000,1,512);a.setParameter(Processor::Amount,25.f);CHECK(!a.controlEstablished());}
 // Off-grid controls run the continuous control law.
 {Processor a;a.prepare(48000,1,512);a.setParameter(Processor::Amount,37.5f);
  std::vector<float> out(in.size());const float* i1[]={in.data()};float* o1[]={out.data()};a.process(i1,o1,unsigned(in.size()));
  Mono m(designMono(48000,controlParameters(.375f)),initialMonoState());
  for(unsigned i=0;i<in.size();++i)CHECK(out[i]==m.process(in[i]));}
 // Bypass On is sample-exact identity (measured at NS1 0 and 100).
 {Processor a;a.prepare(48000,1,512);a.setParameter(Processor::Amount,100.f);a.setParameter(Processor::Bypass,1.f);
  std::vector<float> out(in.size());const float* i1[]={in.data()};float* o1[]={out.data()};a.process(i1,o1,unsigned(in.size()));
  for(unsigned i=0;i<in.size();++i)CHECK(out[i]==in[i]);}
 // Silence in, exact silence out at every control (measured at 0, .25, .5, .75, 1).
 for(float v:{0.f,50.f,100.f}){Processor a;a.prepare(44100,1,64);a.setParameter(Processor::Amount,v);
  std::vector<float> z(9000,0.f);float* o[]={z.data()};const float* i1[]={z.data()};a.process(i1,o,9000);
  for(float x:z)CHECK(x==0.f);}
 std::cout<<"processor contract checks passed\n";
}
