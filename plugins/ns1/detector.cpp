// SPDX-License-Identifier: GPL-3.0-or-later
#include "detector.hpp"
#include <cmath>
namespace tsunami::ns1 {
namespace {
float allpass(float x,const AllpassCoefficients& c,AllpassState& s) noexcept {
    const double y=(double(c.a1)*s.x1+s.x2+double(c.a2*x))
                  -(double(c.a1)*s.y1+double(c.a2)*s.y2);
    s.x2=s.x1; s.x1=x; s.y2=s.y1; s.y1=y;
    return float(y);
}
float fourth(float x) noexcept { const float squared=x*x; return squared*squared; }
float smooth(float target,float old,float retention) noexcept {
    // The retained product rounds in float; interpolation and addition use double.
    const float retained=old*retention;
    return float(double(target)*(1.0-double(retention))+double(retained));
}
}
DetectorResult estimateGain(float input,const DetectorCoefficients& c,DetectorState& s,const DetectorContext& ctx) noexcept {
    const float half=input*0.5f;
    const float cascade=allpass(allpass(half,c.filters[0],s.filters[0]),c.filters[1],s.filters[1]);
    const float parallel=allpass(half,c.filters[2],s.filters[2]);
    const float band=(cascade+parallel)*0.0f+(cascade-parallel);
    const float envelope=std::abs(band)+c.bias;
    const bool active=envelope>=c.activityThreshold;
    const float noiseRate=active?(envelope<=s.noise?c.noiseFall:c.noiseRise):1.0f;
    const float levelRate=active?(envelope<=s.level?c.levelFall:c.levelRise):0.0f;
    s.noise=noiseRate*s.noise;
    const float mixed=(s.level*ctx.noiseToLevel-s.noise)*c.thresholdMix+s.noise;
    const float base=c.baseThreshold*mixed;
    const float relative=mixed*ctx.levelToNoise*c.relativeThreshold;
    const float cap=(c.thresholdCap-ctx.thresholdCorrection)*s.noise;
    const float selected=cap<=relative?cap:relative;
    const float blended=base+c.thresholdBias+(selected-base)*ctx.thresholdBlend;
    const float minimum=c.minimumThreshold*s.noise;
    const float threshold=(minimum<=blended?blended:minimum)*ctx.activityGain;
    s.level=levelRate*s.level+s.level;
    const float threshold4=fourth(threshold);
    const float shape=threshold4/(fourth(envelope)+threshold4);
    const float rawRatio=(ctx.selectedNoise*c.nominalFloor)/s.noise;
    const float ratio=rawRatio<=c.floorCap?rawRatio:c.floorCap;
    const float retention=s.floor<ratio?c.ratioRise:(active?c.ratioFall:1.0f);
    s.floor=smooth(ratio,s.floor,retention);
    const float effectiveFloor=(s.floor-c.nominalFloor)*ctx.adaptive+c.nominalFloor;
    const float target=(effectiveFloor-1.0f)*(shape*shape)+1.0f;
    s.gain=smooth(target,s.gain,target<=s.gain?c.gainFall:c.gainRise);
    return {s.gain,band,envelope,threshold};
}
}
