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
MonoConfiguration designMono(double sampleRate,const DesignParameters&);
MonoConfiguration designSampledMono(double sampleRate,float normalizedControl);
MonoInitialState initialMonoState() noexcept;
}
