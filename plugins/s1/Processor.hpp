// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tsunami::s1 {
class Processor {
public:
 void prepare(double, unsigned, unsigned) {}
 void reset() noexcept {}
 void setParameter(unsigned, float) noexcept {}
 float getParameter(unsigned) const noexcept { return 0; }
 void process(const float* const* inputs, float* const* outputs, unsigned frames) noexcept {
  for (unsigned i=0;i<frames;++i) { const float l=inputs[0][i], r=inputs[1][i]; outputs[0][i]=l; outputs[1][i]=r; }
 }
 unsigned latencySamples() const noexcept { return 0; }
 double tailSeconds() const noexcept { return 0.; }
};
}
