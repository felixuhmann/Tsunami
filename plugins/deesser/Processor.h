// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
namespace tsunami::deesser {
class Processor {
public:
 void prepare(double sampleRate,unsigned channels,unsigned maxBlockSize);
 void reset() noexcept;
 void setParameter(unsigned index,float value) noexcept;
 float getParameter(unsigned index) const noexcept;
 void process(const float* const* inputs,float* const* outputs,unsigned frames) noexcept;
 unsigned latencySamples() const noexcept {return 0;}
 double tailSeconds() const noexcept {return 0;}
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
};
}
