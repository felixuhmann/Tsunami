// SPDX-License-Identifier: GPL-3.0-or-later
// Hosted RVox product wrapper: the shared adapter must drive the actual RVox
// core (not a fixture or passthrough), bit-identically to direct core calls.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using Spec = tsunami::PluginSpec;
int fail(int code, const char* why) { std::cerr << "FAIL " << why << '\n'; return code; }

float physical(juce::AudioProcessorParameter* p) {
    return static_cast<juce::RangedAudioParameter*>(p)->convertFrom0to1(p->getValue());
}
void setPhysical(juce::AudioProcessorParameter* p, float value) {
    p->setValueNotifyingHost(static_cast<juce::RangedAudioParameter*>(p)->convertTo0to1(value));
}
constexpr double pi = 3.14159265358979323846; // M_PI is not standard (MSVC)
std::vector<float> program(unsigned frames, double rate, unsigned channel) {
    std::vector<float> x(frames);
    for (unsigned n = 0; n < frames; ++n) {
        const double t = n / rate;
        const double envelope = .05 + .85 * (0.5 + 0.5 * std::sin(2 * pi * 3.0 * t));
        x[n] = float(envelope * std::sin(2 * pi * (220.0 + 110 * channel) * t));
    }
    return x;
}
// Spread (dB) of 50ms-window RMS levels after the first quarter second.
double levelSpread(const std::vector<float>& x, double rate) {
    const unsigned window = unsigned(rate / 20);
    double lo = 1e9, hi = -1e9;
    for (unsigned at = unsigned(rate / 4); at + window <= x.size(); at += window) {
        double s = 0; for (unsigned n = at; n < at + window; ++n) s += double(x[n]) * x[n];
        const double db = 10 * std::log10(s / window + 1e-30);
        lo = std::min(lo, db); hi = std::max(hi, db);
    }
    return hi - lo;
}
// Renders through the hosted adapter and, in lockstep, through a directly driven core.
int compare(tsunami::Adapter<Spec>& hosted, double rate, int block, std::vector<float> out[2], bool& identical) {
    hosted.prepareToPlay(rate, block);
    Spec::Core direct;
    const auto& params = hosted.getParameters();
    for (int i = 0; i < params.size(); ++i) direct.setParameter(unsigned(i), physical(params[i]));
    direct.prepare(rate, 2, unsigned(block)); direct.reset();
    if (hosted.getLatencySamples() != int(direct.latencySamples()) || direct.latencySamples() == 0)
        return fail(20, "hosted latency does not report recovered RVox lookahead");
    const unsigned frames = unsigned(rate);
    std::vector<float> in[2]{program(frames, rate, 0), program(frames, rate, 1)};
    std::vector<float> ref[2]{std::vector<float>(frames), std::vector<float>(frames)};
    out[0].assign(frames, 0); out[1].assign(frames, 0);
    juce::MidiBuffer midi;
    for (unsigned at = 0; at < frames; at += unsigned(block)) {
        const unsigned n = std::min(unsigned(block), frames - at);
        juce::AudioBuffer<float> b(2, int(n));
        for (int c = 0; c < 2; ++c) b.copyFrom(c, 0, in[c].data() + at, int(n));
        hosted.processBlock(b, midi);
        for (int c = 0; c < 2; ++c) std::memcpy(out[c].data() + at, b.getReadPointer(c), n * sizeof(float));
        const float* ins[]{in[0].data() + at, in[1].data() + at};
        float* outs[]{ref[0].data() + at, ref[1].data() + at};
        direct.process(ins, outs, n);
    }
    identical = out[0] == ref[0] && out[1] == ref[1];
    return 0;
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    if (std::strcmp(Spec::id, "rvox") != 0) return fail(2, "hosted product is not RVox");
    tsunami::Adapter<Spec> p;
    const auto& params = p.getParameters();
    const char* ids[]{"compression", "gate", "gain"};
    const float ranges[][3]{{-36, 0, 0}, {-80, 0, -80}, {-36, 0, 0}};
    if (params.size() != 3) return fail(3, "RVox exposes three host parameters");
    for (int i = 0; i < 3; ++i) {
        auto* ranged = static_cast<juce::RangedAudioParameter*>(params[i]);
        if (ranged->getParameterID() != ids[i] || ranged->getNormalisableRange().start != ranges[i][0]
            || ranged->getNormalisableRange().end != ranges[i][1] || physical(params[i]) != ranges[i][2])
            return fail(4, "parameter id/range/default mismatch");
    }
    setPhysical(params[0], -20.f); setPhysical(params[1], -50.f); setPhysical(params[2], -3.f);
    for (double rate : {44100., 48000., 96000.})
        for (int block : {1, 64, 257}) {
            std::vector<float> out[2]; bool identical = false;
            if (int r = compare(p, rate, block, out, identical)) return r;
            if (!identical) return fail(5, "hosted output differs from direct RVox core");
            // RVox applies automatic makeup, so passthrough shows as unchanged dynamics.
            const double dry = levelSpread(program(unsigned(rate), rate, 0), rate);
            if (!(levelSpread(out[0], rate) < dry - 3)) return fail(6, "hosted module behaves like passthrough");
        }
    juce::MemoryBlock saved; p.getStateInformation(saved);
    setPhysical(params[0], 0.f); setPhysical(params[1], -80.f); setPhysical(params[2], 0.f);
    p.setStateInformation(saved.getData(), int(saved.getSize()));
    if (std::abs(physical(params[0]) + 20) > 1e-3 || std::abs(physical(params[1]) + 50) > 1e-3
        || std::abs(physical(params[2]) + 3) > 1e-3)
        return fail(7, "state restore lost RVox controls");
    tsunami::Adapter<Spec> fresh;
    fresh.setStateInformation(saved.getData(), int(saved.getSize()));
    std::vector<float> a[2], b[2]; bool same = false;
    if (int r = compare(p, 48000, 128, a, same)) return r;
    if (int r = compare(fresh, 48000, 128, b, same)) return r;
    if (a[0] != b[0] || a[1] != b[1]) return fail(8, "fresh restored instance renders differently");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    if (!editor) return fail(9, "no editor");
    std::cout << "rvox.adapter PASS: hosted RVox core bit-identical across 3 rates x 3 blocks, state restore exact\n";
    return 0;
}
