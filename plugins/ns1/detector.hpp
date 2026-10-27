// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
namespace tsunami::ns1 {
struct AllpassCoefficients { float a1=0, a2=0; };
struct AllpassState { double x1=0,x2=0,y1=0,y2=0; };
struct DetectorCoefficients {
    std::array<AllpassCoefficients,3> filters{};
    float bias=0, activityThreshold=0;
    float noiseRise=1,noiseFall=1,levelRise=0,levelFall=0;
    float thresholdMix=0,relativeThreshold=0,thresholdCap=0,baseThreshold=0;
    float thresholdBias=0,minimumThreshold=0;
    float nominalFloor=1,floorCap=1,ratioRise=0,ratioFall=0,gainRise=0,gainFall=0;
};
struct DetectorState {
    std::array<AllpassState,3> filters{};
    float noise=0, level=0, floor=1, gain=1;
};
struct DetectorContext {
    float selectedNoise=0, activityGain=1,levelToNoise=1,noiseToLevel=1;
    float thresholdCorrection=0,adaptive=1,thresholdBlend=1;
};
struct DetectorResult { float gain,band,envelope,threshold; };
DetectorResult estimateGain(float input,const DetectorCoefficients&,DetectorState&,const DetectorContext&) noexcept;
}
