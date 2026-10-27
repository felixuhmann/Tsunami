// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tsunami::fixture {
// Infrastructure-only gain fixture. NOT a target replacement or matched DSP.
class Processor {
    float gain_ = 1;
    unsigned channels_ = 2;
public:
    void prepare(double, unsigned channels, unsigned) { channels_ = channels; }
    void reset() noexcept {}
    void setParameter(unsigned, float gain) noexcept { gain_ = gain; }
    float getParameter(unsigned) const noexcept { return gain_; }
    void process(const float* const* in, float* const* out, unsigned frames) noexcept {
        for (unsigned c=0;c<channels_;++c) for (unsigned n=0;n<frames;++n) out[c][n]=in[c][n]*gain_;
    }
    unsigned latencySamples() const noexcept { return 7; }
    double tailSeconds() const noexcept { return .25; }
};
}
