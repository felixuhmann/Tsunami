// SPDX-License-Identifier: GPL-3.0-or-later
// Header-only DeEsser core (fits tsunami_plugin, which compiles only shared/Plugin.cpp).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>
namespace tsunami::deesser {
class Processor {
public:
 void prepare(double sampleRate,unsigned channels,unsigned maxBlockSize);
 void reset() noexcept;
 // Not thread-safe: call setParameter/prepare/reset on the audio (processing) thread between
 // process() blocks only; the UI must go through the host parameter state, never this object.
 void setParameter(unsigned index,float value) noexcept;
 float getParameter(unsigned index) const noexcept;
 void process(const float* const* inputs,float* const* outputs,unsigned frames) noexcept;
 unsigned latencySamples() const noexcept {return 0;}
 double tailSeconds() const noexcept {return 0;}
 // Realtime-safe meters (lock-free; process() folds each block in with a CAS max, so nothing is lost
 // between UI reads). Values are the maximum since the last take*() (or reset()).
 // gainReductionDb: largest attenuation applied, positive dB (0 = none).
 // detectorLevelDb: peak energy-detector envelope on the Threshold scale, dB (floor -120): the
 //   detector compares envelope/sqrt(0.5) against 10^(threshold/20), so attenuation starts when
 //   the Threshold slider goes below this value (manual ch.2: slider below the blue bar's peak).
 // gainReductionDb()/detectorLevelDb() peek; take*() read and reset (UI thread, any thread safe).
 float gainReductionDb() const noexcept {return gainReductionDb_.load(std::memory_order_relaxed);}
 float detectorLevelDb() const noexcept {return detectorLevelDb_.load(std::memory_order_relaxed);}
 float takeGainReductionDb() noexcept {return gainReductionDb_.exchange(0.f,std::memory_order_relaxed);}
 float takeDetectorLevelDb() noexcept {return detectorLevelDb_.exchange(-120.f,std::memory_order_relaxed);}
 // Current post-block meters for the shared Adapter's CoreMeterSource (observation only; audio is
 // unchanged): meter(0) = detector level and meter(1) = gain reduction, on the same scales as above but
 // for the most recent non-empty process() block only (not held). Read on the processing thread.
 static constexpr unsigned meterCount=2;
 float meter(unsigned index) const noexcept {return index==0?blockDetectorLevelDb_:index==1?blockGainReductionDb_:0.f;}
private:
 std::array<float,6> params_{{5506,0,1,0,0,0}};
 unsigned channels_=1;
 struct Section {double b0=1,b1=0,b2=0,a1=0,a2=0; float x1=0,x2=0,y1=0,y2=0; double tick(double x) noexcept;};
 std::array<Section,4> detector_{};
 double rate_=48000;
 void updateFilters() noexcept;
 std::array<double,1024> curve_{};
 float envelope_=0,gainFast_=1,gainSlow_=1;
 float envelopeRise_=0,envelopeFall_=0;
 std::array<float,2> attack_{},release_{},splitState_{};
 double splitCoefficient_=0;
 unsigned thresholdBin_=1023,detectorScale_=1447;
 void updateThreshold() noexcept;
 static void foldMax(std::atomic<float>& a,float v) noexcept {float cur=a.load(std::memory_order_relaxed);while(v>cur&&!a.compare_exchange_weak(cur,v,std::memory_order_relaxed)){}}
 std::atomic<float> gainReductionDb_{0.f},detectorLevelDb_{-120.f};
 float blockGainReductionDb_=0.f,blockDetectorLevelDb_=-120.f;
};
namespace detail {constexpr double pi=3.14159265358979323846; inline float clip(float x) noexcept{return std::clamp(x,-1.0f,0.999999f);}}
inline double Processor::Section::tick(double x) noexcept {
 const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
 x2=x1;x1=static_cast<float>(x);y2=y1;y1=static_cast<float>(y);return y;
}
inline void Processor::updateFilters() noexcept {
 const double f=std::min(double(params_[0]),rate_*.4999);
 const double theta=2*detail::pi*f/rate_,a=-9.0/11.0;
 splitCoefficient_=(a-std::cos(theta))/(1-a*std::cos(theta)+std::sqrt(1-a*a)*std::sin(theta));
 const double q=2;
 const double upper=std::min(f*(std::sqrt(1+4/(q*q))+2/(q*q)+1)*.5,rate_*.4999);
 for(unsigned k=0;k<4;++k){
  auto& s=detector_[k];
  if(k==3 && params_[2]>.5f){s.b0=1;s.b1=s.b2=s.a1=s.a2=0;continue;}
  const double t=std::tan(detail::pi*(k<3?f*.84:upper)/rate_);
  const double norm=1/(1+std::sqrt(2.0)*t+t*t);
  s.b0=(k<3?1:t*t)*norm;s.b1=(k<3?-2:2)*s.b0;s.b2=s.b0;
  s.a1=2*(t*t-1)*norm;s.a2=(1-std::sqrt(2.0)*t+t*t)*norm;
 }
}
inline void Processor::prepare(double r,unsigned c,unsigned){
 if(!std::isfinite(r)||r<8000||r>384000||c<1||c>2)throw std::invalid_argument("deesser: unsupported sample rate/channel layout");
 rate_=r;channels_=c;
 const auto pole=[r](double ms){return float(std::exp(-1/(r*ms*.001)));};
 envelopeRise_=pole(.00434294);envelopeFall_=pole(.434294);
 attack_={pole(.5),pole(3.98)};
 release_={pole(10),pole(32)};
 const double exponent=1-1/7.0;
 curve_[0]=1;for(unsigned i=1;i<1024;++i)curve_[i]=std::pow(1023.0/i,exponent);
 updateThreshold();updateFilters();reset();
}
inline void Processor::updateThreshold() noexcept {
 const double t=std::max(-30.0,double(params_[1]));
 thresholdBin_=unsigned(std::ceil(1023*std::pow(10.0,t/20)));
 detectorScale_=unsigned(1023/std::sqrt(.5)*std::pow(10.0,(t-params_[1])/20)+.5);
}
inline void Processor::reset() noexcept {for(auto& s:detector_)s.x1=s.x2=s.y1=s.y2=0;envelope_=0;gainFast_=gainSlow_=1;splitState_={0,0};gainReductionDb_.store(0.f,std::memory_order_relaxed);detectorLevelDb_.store(-120.f,std::memory_order_relaxed);blockGainReductionDb_=0.f;blockDetectorLevelDb_=-120.f;}
inline void Processor::setParameter(unsigned i,float v) noexcept {
 if(i>=6||!std::isfinite(v))return;
 params_[i]=i==0?std::round(std::clamp(v,2000.0f,16000.0f)):i==1?std::clamp(v,-80.0f,0.0f):(v>=.5f?1.0f:0.0f);
 if(i==0||i==2)updateFilters();
 if(i==1)updateThreshold();
}
inline float Processor::getParameter(unsigned i) const noexcept {return i<6?params_[i]:0;}
inline void Processor::process(const float* const* in,float* const* out,unsigned n) noexcept {
 double minGain=1;float peakEnvelope=0;
 for(unsigned i=0;i<n;++i){
  float x[2]={in[0][i],0}; if(channels_==2)x[1]=in[1][i];
  if(params_[5]>.5f){for(unsigned c=0;c<channels_;++c)out[c][i]=x[c];continue;}
  for(auto& v:x)if(!std::isfinite(v))v=0; // non-finite input must not poison filter/detector state
  double side=channels_==2?(x[0]+x[1])*.5f:x[0];
  for(auto& s:detector_)side=s.tick(side);
  const float monitored=static_cast<float>(side*std::sqrt(.5));
  const float level=std::abs(monitored);
  envelope_=(envelope_-level)*(level>envelope_?envelopeRise_:envelopeFall_)+level;
  const unsigned bin=unsigned(std::min(1023.0f,envelope_*float(detectorScale_)+.5f));
  const float target=bin<thresholdBin_?1:float(curve_[bin]/curve_[thresholdBin_]);
  const double fast=(double(gainFast_)-target)*(gainFast_<target?release_[0]:attack_[0])+target;
  const double slow=(double(gainSlow_)-target)*(gainSlow_<target?release_[1]:attack_[1])+target;
  gainFast_=float(fast);gainSlow_=float(slow);const double gain=std::min(fast,slow);
  minGain=std::min(minGain,gain);peakEnvelope=std::max(peakEnvelope,envelope_);
  for(unsigned c=0;c<channels_;++c){
   float sample;
   if(params_[3]>.5f)sample=float(x[c]*gain);
   else {const float w=float(x[c]-splitCoefficient_*splitState_[c]);
    const double ap=splitCoefficient_*w+splitState_[c];splitState_[c]=w;
    sample=float((x[c]-ap)*gain+ap+x[c])*.5f;}
   out[c][i]=detail::clip(params_[4]>.5f?monitored:sample);
  }
 }
 if(n==0)return;
 blockGainReductionDb_=minGain<1?float(-20*std::log10(std::max(minGain,1e-6))):0.f;
 blockDetectorLevelDb_=peakEnvelope>1e-6f?float(20*std::log10(double(peakEnvelope)/std::sqrt(.5))):-120.f;
 foldMax(gainReductionDb_,blockGainReductionDb_);
 foldMax(detectorLevelDb_,blockDetectorLevelDb_);
}
}
