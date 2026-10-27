// SPDX-License-Identifier: GPL-3.0-or-later
#include "mono.hpp"
#include <cstdlib>
#include <iostream>
using namespace tsunami::ns1;
int main() {
 MonoConfiguration c{};
 c.activityGain=1;c.minimumActivityGain=1;c.activityOnSamples=1;c.activityOffSamples=1;
 for (auto& b:c.bands) {
   b.detector.bias=.01f;b.detector.activityThreshold=0;b.detector.thresholdCap=1;
   b.detector.baseThreshold=1;b.detector.noiseRise=1;b.detector.noiseFall=1;
   b.detector.nominalFloor=1;b.detector.floorCap=1;b.detector.ratioRise=0;b.detector.ratioFall=0;
   b.detector.gainRise=0;b.detector.gainFall=0;
 }
 MonoInitialState s{};
 for (auto& b:s.bands) { b.noise=1;b.level=1;b.floor=1;b.gain=1; }
 Mono a(c,s),b(c,s);
 float in[128]{},out[128]{};in[0]=1;
 a.process(in,out,128);
 bool nonzero=false;
 for(unsigned i=0;i<128;++i) {const float expected=b.process(in[i]);if(out[i]!=expected)return 1;nonzero|=out[i]!=0;}
 if(!nonzero){std::cerr<<"six-section chain must not be an unconditional mute\n";return 1;}
 a.reset(s);if(a.process(1)!=out[0])return 1;
 std::cout<<"mono partition and explicit-state reset checks passed\n";
}
