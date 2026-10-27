// SPDX-License-Identifier: GPL-3.0-or-later
#include "design.hpp"
#include <cmath>
#include <stdexcept>
namespace tsunami::ns1 {
namespace {
constexpr double pi=3.1415926535897932384626433832795;
double gain(double db){return std::pow(10.0,db/20.0);}
double pole(double seconds,double rate){return std::exp(-1.0/(seconds*rate));}
double boundedFrequency(double frequency){return std::round(frequency<1?1:(frequency>20000?20000:frequency));}
AllpassCoefficients simple(double f,double q,double rate){
 const double t=std::tan(pi*f/(rate*q)),a2=(1-t)/(1+t);
 return {float(-std::cos(2*pi*f/rate)*(1+a2)),float(a2)};
}
double first(double f,double rate){const double w=2*pi*f/rate;return -std::cos(w)/(1+std::sin(w));}
AllpassCoefficients warped(double f,double width,double rate){
 const double t=std::tan(std::atan(0.5)/width),r=(1-t)/(1+t);
 const double w=2*pi*f/rate;
 const double h=(std::sin(w)-1.0)/std::cos(w);
 const double scale=1.0/(1.0+r*h*h);
 return {float(2*scale*h*(r+1)),float((h*h+r)*scale)};
}
}
MonoInitialState initialMonoState() noexcept {
 MonoInitialState s{};
 // Initial detector state for the six bands: these 24 constants were
 // recovered from the vendor binary analysis (initialization routine
 // 0x180026950). They are recovered constants, not semantic or derived values.
 // Not a serialized memory image; all audio/allpass histories begin at zero.
 auto band=[&](unsigned i,double noise,double level,double floor,double gain){
  s.bands[i].noise=float(noise);s.bands[i].level=float(level);
  s.bands[i].floor=float(floor);s.bands[i].gain=float(gain);
 };
 band(0,.00017582483,.011304817,.44327036,.9949857);
 band(1,.00027715639,.05510236,.33406392,.99922985);
 band(2,.00024879278,.09911944,.31819442,.99901527);
 band(3,.00025951385,.046371099,.32851437,.99930668);
 band(4,.00022133121,.050231483,.37519994,.98898983);
 band(5,.00013427735,.021433139,.46784535,.97664964);
 s.activityGain=float(gain(3));return s;
}
DesignParameters sampledParameters(float control){
 if(control!=0&&control!=0.5f&&control!=1)throw std::invalid_argument("only sampled controls 0, 0.5 and 1 are established");
 DesignParameters p{};
 p.lowerHz=control==0?60:(control==0.5f?70:110);
 p.upperHz=control==0?16000:(control==0.5f?13000:8000);
 p.attenuationDb=control==0?0:(control==0.5f?-40:-70);
 p.relativeThreshold=control==0?0:(control==.5f?.045f:.8f);
 p.nominalFloor=control==0?1:float(gain(-18.6*17.9/32));
 p.floorCap=control==0?1:float(gain(-6));
 return p;
}
MonoConfiguration designSampledMono(double rate,float control){return designMono(rate,sampledParameters(control));}
MonoConfiguration designMono(double rate,const DesignParameters& p){
 if(!std::isfinite(rate)||rate<40000||rate>100000)throw std::invalid_argument("unvalidated sample rate");
 const double lower=p.lowerHz,upper=p.upperHz;
 const double attenuation=p.attenuationDb==0?1:gain(p.attenuationDb);
 const double shape=upper<14000?1:(upper>=17000?3:1+2*(upper-14000)/3000);
 MonoConfiguration c{};
 c.front[0]={simple(upper,shape,rate),float(first(upper,rate)),float((1-attenuation)/2),float(-(1+attenuation)/2)};
 c.front[1]={simple(lower,1,rate),float(first(lower,rate)),float((attenuation-1)/2),float(-(1+attenuation)/2)};
 const double spacing=std::pow(upper/lower,1.0/15.0);
 const double extent=std::pow(lower/upper,1.0/12.0);
 double center=lower;
 for(unsigned k=0;k<6;++k){
  const double roundedCenter=boundedFrequency(center);
  auto& band=c.bands[k];auto& d=band.detector;
  const double low=boundedFrequency(roundedCenter*extent),high=boundedFrequency(roundedCenter/extent);
  d.filters[0]=warped(high,1,rate);d.filters[1]=warped(low,1,rate);
  const double a=first(low,rate),b=first(high,rate);
  d.filters[2]={float(a+b),float(a*b)};
  d.bias=float(gain(-100));d.activityThreshold=float(gain(-80));
  d.noiseRise=float(2-pole(1.002,rate));d.noiseFall=float(pole(.05,rate));
  d.levelRise=float(1-pole(.08,rate));d.levelFall=float(pole(10.01,rate)-1);
  d.thresholdMix=1;d.relativeThreshold=p.relativeThreshold;
  d.thresholdCap=float(gain(35));d.baseThreshold=float(gain(30)*std::sqrt(1002.0/50));
  d.thresholdBias=float(gain(-100));d.minimumThreshold=1;
  d.nominalFloor=p.nominalFloor;
  d.floorCap=p.floorCap;
  d.ratioRise=d.ratioFall=float(pole(2,rate));
  d.gainRise=float(pole(.0024,rate));d.gainFall=float(pole(.072,rate));
  band.activityRatio=k<4?float(gain(-32)):0;
  double node=roundedCenter/spacing;
  for(unsigned j=0;j<3;++j){
   const double f=boundedFrequency(node),p=std::pow((f*2)/rate,3);
   const double width=((p+.5)/(p*3.5+.5))*extent*1.8*extent;
   band.audio[j]=warped(f,width<.5?.5:width,rate);node=node*spacing;
  }
  center=center*spacing*spacing*spacing;
 }
 c.activityOnSamples=unsigned(std::round(rate*.001));c.activityOffSamples=unsigned(std::round(rate*.06));
 c.minimumActivityGain=float(gain(3));
 return c;
}
}
