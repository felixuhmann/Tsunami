// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mono.hpp"
namespace tsunami::ns1 {
// Experimental: only the three measured controls; initialization is supplied separately.
// Control-dependent internal design quantities (the only fields of the design
// that differ between the measured controls 0, 0.5 and 1).
struct DesignParameters {
 double lowerHz=60, upperHz=16000, attenuationDb=0;
 float relativeThreshold=0, nominalFloor=1, floorCap=1;
};
DesignParameters sampledParameters(float normalizedControl);
// Continuous NS1 control law for normalizedControl in [0,1] (clamped). Piecewise
// linear with breakpoints 0.2 and 0.8 (floor cap linear in dB on [0,0.2]).
// Segment values: 0/0.5/1 anchors from calibrated captures, the 0.2-0.8 slope
// from the bundled factory-preset internal values (NS1 25/30/50/80 %), the
// outer segments from oracle renders at 10 % and 90 %. Equals
// sampledParameters() at 0, 0.5 and 1.
DesignParameters controlParameters(float normalizedControl);
MonoConfiguration designMono(double sampleRate,const DesignParameters&);
MonoConfiguration designSampledMono(double sampleRate,float normalizedControl);
MonoInitialState initialMonoState() noexcept;
}
