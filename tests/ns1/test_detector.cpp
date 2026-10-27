// SPDX-License-Identifier: GPL-3.0-or-later
#include "detector.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace tsunami::ns1;
void require(bool v, const char* what) { if (!v) { std::cerr<<what<<'\n'; std::exit(1); } }
int main() {
    DetectorCoefficients c{};
    c.bias=0.02f; c.activityThreshold=0.01f;
    c.noiseRise=1; c.noiseFall=1; c.levelRise=0; c.levelFall=0;
    c.thresholdMix=0; c.relativeThreshold=1; c.thresholdCap=1;
    c.baseThreshold=1; c.minimumThreshold=0; c.thresholdBias=0;
    c.nominalFloor=1; c.floorCap=1; c.ratioRise=0; c.ratioFall=0;
    c.gainRise=0; c.gainFall=0;
    DetectorState s{}; s.noise=2; s.level=2; s.floor=1; s.gain=1;
    DetectorContext ctx{}; ctx.selectedNoise=0.5f; ctx.activityGain=1;
    ctx.levelToNoise=1; ctx.noiseToLevel=1; ctx.adaptive=1; ctx.thresholdBlend=1;
    auto result=estimateGain(0,c,s,ctx);
    require(std::abs(s.floor-0.25f)<1e-7f,"adaptive interband ratio must reduce nominal unity floor");
    require(result.gain<0.251f,"silence reaches adaptive rather than nominal floor");
    auto t=s; ctx.adaptive=0;
    result=estimateGain(0,c,t,ctx);
    require(result.gain==1,"disabled adaptation uses nominal floor");
    std::cout<<"detector unit checks passed\n";
}
