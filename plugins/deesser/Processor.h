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
};
}
