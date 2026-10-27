// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <vector>
namespace tsunami::rvox {
// Independent equation implementation. See evidence for validated scope.
// Numeric-domain policy (replacement-only, not vendor parity): nonfinite input
// samples are processed as silence and allpass detector state saturates at
// +/-FLT_MAX, so finite overload cannot yield Inf*0 or NaN gain state. Both
// are identities for every ordinary finite signal, including >0dBFS audio.
class Processor {
public:
 void prepare(double rate, unsigned channels, unsigned) {
  if (!std::isfinite(rate) || rate <= 0 || channels < 1 || channels > 2)
   throw std::invalid_argument("rvox needs a positive finite rate, mono/stereo");
  // Hosts may prepare at any positive rate; a throw would abort them. Outside the
  // recovered 8-384kHz domain the core designs at the nearest supported rate.
  rate=std::min(std::max(rate,8000.),384000.);
  rate_=rate; channels_=channels; lookahead_=1;
  while (lookahead_<static_cast<unsigned>(rate/1000)) lookahead_*=2;
  widthA_=static_cast<unsigned>(std::floor(lookahead_*.422+.5));
  widthB_=lookahead_-widthA_+1; shift_=0;
  while ((1u<<shift_) < widthA_*widthB_) ++shift_;
  normalization_=double(1u<<shift_)/(widthA_*widthB_);
  minima_.assign(2*lookahead_,0);
  quantized_.assign(lookahead_+2,0);
  for (auto& d:delay_) d.assign(lookahead_,0);
  slowDown_=pole(250); slowUp_=pole(500);
  // Fresh Windows state stores the hidden gate release in RealWorld slot 2.
  // It is 130ms; this is not the compressor's level-dependent release.
  gateRelease_=pole(130);
  generateRelease(); updateControls(); reset();
 }
 void reset() noexcept {
  for(auto& a:allpass_) a.fill(0);
  for(auto& d:delay_) std::fill(d.begin(),d.end(),0);
  std::fill(minima_.begin(),minima_.end(),1);
  std::fill(quantized_.begin(),quantized_.end(),INT32_MAX>>shift_);
  index_=0; gateEnvelope_=0; slow_=target_=1; release_=pole(1);
  accumulator1_=accumulator2_=int64_t(widthA_)*widthB_*(int64_t(1)<<(31-shift_));
 }
 void setParameter(unsigned index, float value) noexcept {
  if(index>=3 || !std::isfinite(value)) return;
  parameters_[index]=std::clamp(value,index==1?-80.f:-36.f,0.f); updateControls();
 }
 float getParameter(unsigned index) const noexcept {return index<3?parameters_[index]:0;}
 unsigned latencySamples() const noexcept { return lookahead_; }
 double tailSeconds() const noexcept { return double(lookahead_)/rate_; }
 float makeupCoefficient() const noexcept { return makeup_; }
 // Project-defined taps of state the core already computes, read after a block
 // (no effect on audio; not a proven reproduction of the original meter scale
 // or ballistics). kEnergy: linear full-scale detector envelope (allpass-rotated
 // peak with the 130ms gate release), >=0. kTotalGain: linear gate*compressor
 // gain after the lookahead smoother and before makeup, aligned with the delayed
 // output; 1 means no reduction. Unprepared/out-of-range reads are idle values.
 enum : unsigned { kEnergy, kTotalGain };
 static constexpr unsigned meterCount=2;
 float meter(unsigned index) const noexcept {
  if(index==kEnergy) return gateEnvelope_;
  if(index!=kTotalGain) return 0;
  if(minima_.empty()) return 1;
  return static_cast<float>(double(accumulator1_)*4.656613e-10*normalization_);
 }
 void process(const float* const* input,float* const* output,unsigned frames) noexcept {
  if(minima_.empty()) return;
  constexpr float ap[3]={.6303f,.3583f,.1607f};
  for(unsigned n=0;n<frames;++n) {
   const auto finite=[](float x) noexcept {return std::isfinite(x)?x:0.f;};
   float samples[2]={finite(input[0][n]), channels_==2?finite(input[1][n]):0.f};
   float peak=0;
   for(unsigned c=0;c<channels_;++c) {
    const float x=samples[c]; peak=std::max(peak,std::abs(x));
    for(unsigned k=0;k<3;++k) {
     auto& state=allpass_[c];
     const float v=std::clamp((x-state[k*2+1])*ap[k]+state[k*2],-FLT_MAX,FLT_MAX);
     state[k*2]=x; state[k*2+1]=v; peak=std::max(peak,std::abs(v));
    }
   }
   gateEnvelope_=peak>=gateEnvelope_?peak:peak+(gateEnvelope_-peak)*gateRelease_;
   const float gate=parameters_[1]<=-79?1.f:std::min(1.f,gateEnvelope_*gateScale_);
   float desired=threshold_/(peak*(1.-double(threshold_))+threshold_);
   unsigned node=lookahead_+index_%lookahead_; minima_[node]=desired;
   while(node>1) {node/=2; minima_[node]=std::min(minima_[node*2],minima_[node*2+1]);}
   desired=minima_[1];
   slow_=desired+(slow_-desired)*(desired<slow_?slowDown_:slowUp_);
   float following=std::min(slow_,desired);
   if(target_<=following) following=following+(target_-following)*release_;
   target_=following;
   const auto tableIndex=[](float v) noexcept {return static_cast<unsigned>(std::clamp(v*255.f+.5f,0.f,255.f));};
   const unsigned recoveryIndex=tableIndex(float((double(slow_)*2-target_)));
   release_=releaseShape_[tableIndex(slow_)]*(recoveryIndex==0?releaseShape_[255]:releaseBase_[recoveryIndex-1]);
   const double scaled=double(gate*target_)*2147483600.;
   const int32_t q=static_cast<int32_t>(std::clamp(scaled,0.,2147483647.))>>shift_;
   const unsigned len=static_cast<unsigned>(quantized_.size());
   auto history=[&](unsigned age) noexcept {return quantized_[(index_+len-age%len)%len];};
   const int64_t next=int64_t(q)-history(widthA_)-history(widthB_)+history(widthA_+widthB_)+2*accumulator1_-accumulator2_;
   quantized_[index_%len]=q; accumulator2_=accumulator1_; accumulator1_=next;
   const float smooth=static_cast<float>(next)*4.656613e-10;
   const float gain=makeup_*smooth;
   for(unsigned c=0;c<channels_;++c) {
    const unsigned slot=index_%lookahead_; const float delayed=delay_[c][slot];
    delay_[c][slot]=samples[c]; output[c][n]=delayed*gain;
   }
   ++index_;
  }
 }
private:
 float pole(double ms) const noexcept {return static_cast<float>(std::pow(.367879441,1./(rate_*ms*.001)));}
 void updateControls() noexcept {
  // Recovered builder: hot-loop threshold is float, makeup uses the double one.
  const double threshold=std::pow(10.,parameters_[0]/20.);
  threshold_=static_cast<float>(threshold);
  makeup_=static_cast<float>(std::pow(10.,parameters_[2]/20.)*(normalization_*.99/threshold));
  gateScale_=static_cast<float>(std::pow(10.,-parameters_[1]/20.));
 }
 void generateRelease() noexcept {
  const double invLog=1/std::log(10.); std::array<double,256> base{};
  for(unsigned i=0;i<256;++i) {
   const double t=std::max(-std::log(.1)/rate_,std::pow(1-double(i)/256,4.));
   base[i]=std::pow(.367879441,1./(t*invLog*rate_)); releaseBase_[i]=static_cast<float>(base[i]);
  }
  const unsigned quarter=64, half=128, upper=180;
  const double middle=std::pow(.367879441,1./(invLog*.030*rate_))/base[half];
  const double low=std::pow(.367879441,1./(invLog*.150*rate_))/base[quarter];
  const double high=std::pow(.367879441,1./(invLog*.0005*rate_))/base[upper];
  for(unsigned i=0;i<256;++i) {
   double v=1;
   if(i>=upper) v=middle*std::pow(high/middle,double(i-upper+1)/(256-upper));
   else if(i>=half) v=low*std::pow(middle/low,double(i-half+1)/(upper-half));
   else if(i>=quarter) v=std::pow(low,double(i-quarter+1)/(half-quarter));
   releaseShape_[i]=static_cast<float>(v);
  }
 }
 double rate_=48000,normalization_=1;
 unsigned channels_=1,lookahead_=64,widthA_=27,widthB_=38,shift_=11;
 uint64_t index_=0;
 std::array<float,3> parameters_{{0,-80,0}};
 std::array<std::array<float,6>,2> allpass_{};
 std::array<std::vector<float>,2> delay_;
 std::vector<float> minima_;
 std::vector<int32_t> quantized_;
 std::array<float,256> releaseShape_{},releaseBase_{};
 float threshold_=1,makeup_=.99f,gateScale_=1,gateEnvelope_=0,slow_=1,target_=1,release_=1;
 float slowDown_=1,slowUp_=1,gateRelease_=1;
 int64_t accumulator1_=0,accumulator2_=0;
};
}
