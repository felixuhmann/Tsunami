// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tsunami::s1 {
// Independent S1-Imager recovery. No Shuffler/MS-Matrix component emulation.
// Control calls and processing must be serialized by the adapter/audio thread.
class Processor {
public:
    enum Parameter : unsigned { Gain, Width, Rotation, Asymmetry, Swap,
                                InvertLeft, InvertRight, InputMode, Bypass, Count };
    void prepare(double sampleRate, unsigned channels, unsigned /*maxBlockSize*/) {
        channels_ = channels;
        const double rate = std::isfinite(sampleRate) && sampleRate > 0. ? sampleRate : 48000.;
        smoothing_ = float(std::pow(0.36787944117, 1. / (rate * .040)));
        reset();
    }
    // Preserve physical controls; snap to current target, no residual audio state.
    void reset() noexcept {
        updateMatrix();
        for (unsigned k=0; k<4; ++k) current_[k]=target_[k];
    }
    void setParameter(unsigned index, float value) noexcept {
        if (index>=Count || !std::isfinite(value)) return;
        constexpr std::array<float,Count> lo{{-24,0,-45,-90,0,0,0,0,0}};
        constexpr std::array<float,Count> hi{{0,3,45,90,1,1,1,1,1}};
        value=std::clamp(value,lo[index],hi[index]);
        parameters_[index]=index>=Swap ? float(value>=.5f) : value;
        updateMatrix();
    }
    float getParameter(unsigned index) const noexcept { return index<Count ? parameters_[index] : 0.f; }
    void process(const float* const* inputs, float* const* outputs, unsigned frames) noexcept {
        if (!frames) return;
        // Reference component is stereo-only. Other layouts are safety passthrough,
        // explicitly not a claimed mono or multichannel version of the reference.
        if (channels_!=2) {
            for(unsigned c=0;c<channels_;++c)
                for(unsigned n=0;n<frames;++n) outputs[c][n]=inputs[c][n];
            return;
        }
        for(unsigned k=0;k<4;++k)
            if(std::abs(float(current_[k]-target_[k]))<1e-7f) current_[k]=target_[k];
        for(unsigned n=0;n<frames;++n) {
            const double left=inputs[0][n], right=inputs[1][n];
            for(unsigned k=0;k<4;++k)
                current_[k]=target_[k]+(current_[k]-target_[k])*double(smoothing_);
            outputs[0][n]=parameters_[Bypass] ? float(left) : float(current_[0]*left+current_[1]*right);
            outputs[1][n]=parameters_[Bypass] ? float(right) : float(current_[3]*left+current_[2]*right);
        }
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return 0.; }
private:
    void updateMatrix() noexcept {
        const double width=parameters_[Width];
        const double normalization=width<=1. ? std::sqrt(.5) : std::sqrt((1.+.6/(width*width))/3.2);
        const double gain=normalization*std::pow(10.,parameters_[Gain]*.05);
        constexpr double radians=0.017453292519943295;
        const double angle=(45.-parameters_[Rotation])*radians;
        const double skew=angle+parameters_[Asymmetry]*radians;
        const double midL=std::sin(angle)*gain, midR=std::cos(angle)*gain;
        const double sideL=width*std::cos(skew)*gain, sideR=width*std::sin(skew)*gain;
        double a=midL+sideL,b=midL-sideL,c=midR+sideR,d=midR-sideR;
        if(parameters_[InputMode]) { const double oldA=a,oldD=d; a+=b;b=oldA-b;d+=c;c=oldD-c; }
        if(parameters_[InvertLeft]) {a=-a;d=-d;}
        if(parameters_[InvertRight]) {b=-b;c=-c;}
        if(parameters_[Swap]) {std::swap(a,b);std::swap(c,d);}
        target_={{float(a),float(b),float(c),float(d)}};
    }
    std::array<float,Count> parameters_{{0,1,0,0,0,0,0,0,0}};
    std::array<float,4> target_{{1,0,1,0}};
    std::array<double,4> current_{{1,0,1,0}};
    float smoothing_=0.f;
    unsigned channels_=2;
};
} // namespace tsunami::s1
