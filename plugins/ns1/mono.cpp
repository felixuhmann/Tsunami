// SPDX-License-Identifier: GPL-3.0-or-later
#include "mono.hpp"
namespace tsunami::ns1 {
void Mono::reset(const MonoInitialState& s) noexcept {front_={};audio_={};envelope_={};detector_=s.bands;activityGain_=s.activityGain;on_=s.activityOnCounter;off_=s.activityOffCounter;}
namespace {
double frontAllpass(double x,const AllpassCoefficients& c,AllpassState& s) noexcept {
 const double y=(double(c.a1)*s.x1+x*double(c.a2)+s.x2)-(double(c.a1)*s.y1+double(c.a2)*s.y2);
 s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=y;return y;
}
float audioAllpass(float x,const AllpassCoefficients& c,AllpassState& s) noexcept {
 const double y=(s.x2+double(c.a1)*s.x1+double(c.a2*x))-(double(c.a1)*s.y1+double(c.a2)*s.y2);
 s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=y;return float(y);
}
}
float Mono::process(float input) noexcept {
 float front=input;
 for(unsigned k=0;k<2;++k) {
  const auto& c=config_.front[k];auto& s=front_[k];
  const double first=double(c.first)*double(front)-(double(c.first)*s.previousOutput-s.previousInput);
  s.previousInput=front;s.previousOutput=first;
  front=float(double(c.firstWeight)*first-double(c.secondWeight)*frontAllpass(front,c.second,s.second));
 }
 float sumNoise=0,sumLevel=0,minRatio=0;unsigned selected=0,activity=0;
 for(unsigned k=0;k<6;++k) {
  sumNoise=sumNoise+detector_[k].noise;sumLevel=sumLevel+detector_[k].level;
  const float ratio=config_.bands[k].detector.nominalFloor/detector_[k].noise;
  if(k==0||ratio<minRatio){selected=k;minRatio=ratio;}
  // The envelope from the previous sample determines the global activity latch.
  // Its storage is separate from the detector's smoothed level.
 }
 const float levelToNoise=(sumLevel*(1.0f/6.0f))/(sumNoise*(1.0f/6.0f));
 const float selectedNoise=detector_[selected].noise;
 const float rawCorrection=levelToNoise*levelToNoise*0.000275786f;
 const float correction=rawCorrection<=44.12576f?rawCorrection:44.12576f;
 for(unsigned k=0;k<=config_.activityBandLast&&k<6;++k)
  if(detector_[k].noise<envelope_[k]*config_.bands[k].activityRatio)++activity;
 if(activity<config_.activityBandCount){on_=0;if(++off_>=config_.activityOffSamples){off_=config_.activityOffSamples;activityGain_=config_.minimumActivityGain;}}
 else {off_=0;if(++on_>=config_.activityOnSamples){on_=config_.activityOnSamples;activityGain_=1;}}
 DetectorContext ctx{selectedNoise,activityGain_,levelToNoise,1.0f/levelToNoise,correction,config_.adaptive,config_.thresholdBlend};
 float audio=front;
 for(unsigned k=0;k<6;++k) {
  const auto& c=config_.bands[k];
  const auto result=estimateGain(front,c.detector,detector_[k],ctx);envelope_[k]=result.envelope;
  const float gain=result.gain*c.scale;
  float stage=audio*0.5f;
  for(unsigned j=0;j<3;++j) {
   const float filtered=audioAllpass(stage,c.audio[j],audio_[k][j]);
   const float mix=gain*(stage-filtered);
   stage=stage+filtered+mix;
   if(j!=2)stage=stage*0.5f;
  }
  audio=stage;
 }
 return audio;
}
void Mono::process(const float* in,float* out,std::size_t n) noexcept {for(std::size_t i=0;i<n;++i)out[i]=process(in[i]);}
}
