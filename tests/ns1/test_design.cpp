// SPDX-License-Identifier: GPL-3.0-or-later
#include "design.hpp"
#include <cmath>
#include <iostream>
int main() {
 using namespace tsunami::ns1;
 const auto zero=designSampledMono(48000,0);
 const auto half=designSampledMono(48000,.5f);
 const auto full=designSampledMono(48000,1);
 if(!(half.bands[0].detector.nominalFloor<zero.bands[0].detector.nominalFloor))return 1;
 if(!(full.bands[0].detector.relativeThreshold>half.bands[0].detector.relativeThreshold))return 1;
 if(!(half.front[0].firstWeight>0))return 1;
 bool rejected=false;try{designSampledMono(48000,.25f);}catch(...){rejected=true;}
 if(!rejected)return 1;
 // Continuous control law (piecewise linear, breakpoints 0.2/0.8; floor cap dB-linear on [0,0.2]).
 auto near=[](double a,double b,double tol){return std::fabs(a-b)<=tol;};
 const double cap6=std::pow(10.0,-6/20.0),cap3=std::pow(10.0,-3/20.0);
 const auto p=controlParameters(.375f);
 if(!(near(p.lowerHz,65.833333,1e-4)&&near(p.upperHz,13416.6667,1e-2)&&near(p.attenuationDb,-27.5,1e-4)&&near(p.relativeThreshold,.03458333,1e-7)&&near(p.floorCap,cap6,1e-6)&&near(p.nominalFloor,half.bands[0].detector.nominalFloor,1e-9)))return 2;
 const auto q=controlParameters(.1f);
 if(!(near(q.lowerHz,60,1e-4)&&near(q.upperHz,15000,1e-1)&&near(q.attenuationDb,-5,1e-4)&&near(q.relativeThreshold,.01,1e-7)&&near(q.floorCap,cap3,1e-6)))return 3;
 const auto r=controlParameters(.9f);
 if(!(near(r.lowerHz,95,1e-4)&&near(r.upperHz,10000,1e-1)&&near(r.attenuationDb,-70,1e-6)&&near(r.relativeThreshold,.435,1e-6)&&near(r.floorCap,cap6,1e-6)))return 4;
 for(float c:{0.f,.5f,1.f}){const auto a=controlParameters(c),b=sampledParameters(c);
  if(a.lowerHz!=b.lowerHz||a.upperHz!=b.upperHz||a.attenuationDb!=b.attenuationDb||a.relativeThreshold!=b.relativeThreshold||a.nominalFloor!=b.nominalFloor||a.floorCap!=b.floorCap)return 5;}
 // Front filters use the 1 Hz-resolution edge frequencies; the band layout uses the unrounded ones.
 {DesignParameters b=p;b.lowerHz=66;b.upperHz=13417;
  const auto da=designMono(48000,p),db=designMono(48000,b);
  for(unsigned k=0;k<2;++k)if(da.front[k].second.a1!=db.front[k].second.a1||da.front[k].first!=db.front[k].first)return 6;
  if(da.bands[1].audio[0].a1==db.bands[1].audio[0].a1)return 7;}
 std::cout<<"sampled-control design checks passed\n";
}
