// SPDX-License-Identifier: GPL-3.0-or-later
// Hosted RVox product wrapper: the shared adapter must drive the actual RVox
// core (not a fixture or passthrough), bit-identically to direct core calls.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <iostream>
#include <string>
#include <vector>

// Counts heap allocations while `countAllocations` is set (warm audio callbacks must not allocate).
static std::atomic<bool> countAllocations{false};
static std::atomic<long> allocations{0};
void* operator new(std::size_t n) {
    if (countAllocations.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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
// Streams the shared program through the hosted adapter and a direct core in
// lockstep; before each block `atBlock` may change host-side controls and must
// mirror the same physical values into the direct core (block-boundary automation).
template<class F>
bool lockstep(tsunami::Adapter<Spec>& hosted, Spec::Core& direct, double rate, int block, unsigned frames, F atBlock) {
    std::vector<float> in[2]{program(frames, rate, 0), program(frames, rate, 1)};
    juce::MidiBuffer midi; bool identical = true; std::vector<float> ref[2]{std::vector<float>(size_t(block)), std::vector<float>(size_t(block))};
    for (unsigned at = 0, k = 0; at < frames; at += unsigned(block), ++k) {
        atBlock(k);
        const auto& params = hosted.getParameters();
        for (int i = 0; i < params.size(); ++i) direct.setParameter(unsigned(i), physical(params[i]));
        const unsigned n = std::min(unsigned(block), frames - at);
        juce::AudioBuffer<float> b(2, int(n));
        for (int c = 0; c < 2; ++c) b.copyFrom(c, 0, in[c].data() + at, int(n));
        hosted.processBlock(b, midi);
        const float* ins[]{in[0].data() + at, in[1].data() + at};
        float* outs[]{ref[0].data(), ref[1].data()};
        direct.process(ins, outs, n);
        for (int c = 0; c < 2; ++c) identical = identical && std::memcmp(ref[c].data(), b.getReadPointer(c), n * sizeof(float)) == 0;
    }
    return identical;
}
juce::MemoryBlock withValue(const juce::MemoryBlock& saved, const char* id, const char* value) {
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize()));
    for (auto* e : xml->getChildIterator()) if (e->getStringAttribute("id") == id) e->setAttribute("value", value);
    juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary(*xml, out); return out;
}
// Automation / preset hidden-state matrix through the actual hosted product.
int hiddenStateMatrix() {
    for (int block : {1, 64, 257}) {
        tsunami::Adapter<Spec> p; const auto& params = p.getParameters();
        p.prepareToPlay(48000, block); Spec::Core direct; direct.prepare(48000, 2, unsigned(block));
        juce::MemoryBlock a, b;
        setPhysical(params[0], -24.f); setPhysical(params[1], -45.f); setPhysical(params[2], -6.f); p.getStateInformation(a);
        setPhysical(params[0], -9.f); setPhysical(params[1], -80.f); setPhysical(params[2], -1.5f); p.getStateInformation(b);
        const unsigned frames = 48000, blocks = frames / unsigned(block);
        const bool same = lockstep(p, direct, 48000, block, frames, [&](unsigned k) {
            if (k == blocks / 8) setPhysical(params[2], -12.f);                       // gain automation
            if (k == blocks / 4) setPhysical(params[1], -30.f);                       // gate engage
            if (k == blocks / 3) for (int i = 0; i < 3; ++i) setPhysical(params[i], physical(params[i])); // re-send
            if (k == blocks / 2) p.setStateInformation(a.getData(), int(a.getSize()));  // preset recall
            if (k == 2 * blocks / 3) p.setStateInformation(a.getData(), int(a.getSize())); // same preset again
            if (k == 3 * blocks / 4) p.setStateInformation(b.getData(), int(b.getSize())); // other preset
            if (k == 7 * blocks / 8) setPhysical(params[0], -36.f);                   // compression automation
        });
        if (!same) return fail(30, "hosted automation/preset recall differs from block-boundary core schedule");
        if (std::abs(physical(params[0]) + 36) > 1e-3 || std::abs(physical(params[1]) + 80) > 1e-3) return fail(31, "final controls");
    }
    // Hostile/legacy preset values: host-visible controls stay finite and in
    // range, and the core renders exactly what the host shows.
    tsunami::Adapter<Spec> p; const auto& params = p.getParameters(); p.prepareToPlay(48000, 128);
    setPhysical(params[0], -18.f); setPhysical(params[1], -40.f); setPhysical(params[2], -3.f);
    juce::MemoryBlock saved; p.getStateInformation(saved);
    const float lo[]{-36, -80, -36};
    auto inRange = [&] { for (int i = 0; i < 3; ++i) { const float v = physical(params[i]);
        if (!std::isfinite(v) || v < lo[i] || v > 0) return false; } return true; };
    const char* hostile[][2]{{"gate", "-200"}, {"compression", "12"}, {"compression", "inf"}, {"gate", "garbage"}};
    for (auto& h : hostile) {
        const auto state = withValue(saved, h[0], h[1]);
        p.setStateInformation(state.getData(), int(state.getSize()));
        if (!inRange()) return std::cerr << h[0] << '=' << h[1] << '\n', fail(32, "hostile preset value escaped the control range");
        Spec::Core direct; direct.prepare(48000, 2, 128);
        p.reset();
        if (!lockstep(p, direct, 48000, 128, 9600, [](unsigned) {})) return fail(33, "core diverges from host-visible control after hostile preset");
    }
    // A nonfinite preset value is rejected by the shared adapter's transactional
    // state restore (merged main), so the host never sees NaN.
    {
        const auto state = withValue(saved, "gain", "nan");
        p.setStateInformation(state.getData(), int(state.getSize()));
        if (!inRange()) return fail(56, "nonfinite preset value reached host parameters");
    }
    std::cout << "rvox.adapter hidden-state matrix PASS: block-boundary automation, preset recall, hostile preset values\n";
    return 0;
}
// Round-2 task3: +20 dBFS sine at Comp -6 / Gain -28.7 through the hosted
// adapter. Output must equal the core, its peak must equal the genuine-confirmed
// 0.06624011 (5.1 dB above the Gain value, as the original does), and the meter
// tap the editor reads must report exactly the per-channel output buffer peak.
int hotInputMeter() {
    tsunami::Adapter<Spec> p; const auto& ps = p.getParameters();
    setPhysical(ps[0], -6.f); setPhysical(ps[1], -80.f); setPhysical(ps[2], -28.7f);
    Spec::Core direct;
    for (int i = 0; i < 3; ++i) direct.setParameter(unsigned(i), physical(ps[i]));
    p.prepareToPlay(48000, 512); direct.prepare(48000, 2, 512); direct.reset();
    juce::MidiBuffer midi; float hostedPeak = 0, held[2]{};
    for (unsigned at = 0; at < 24000; at += 512) {
        const int n = int(std::min(512u, 24000 - at));
        juce::AudioBuffer<float> b(2, n);
        std::vector<float> x[2]{std::vector<float>(size_t(n)), std::vector<float>(size_t(n))}, y[2]{x[0], x[1]};
        for (int i = 0; i < n; ++i) {
            x[0][size_t(i)] = float(10 * std::sin(2 * pi * 997 * (at + unsigned(i)) / 48000));
            x[1][size_t(i)] = float(5 * std::sin(2 * pi * 997 * (at + unsigned(i)) / 48000 + 1.0));
        }
        for (int c = 0; c < 2; ++c) b.copyFrom(c, 0, x[c].data(), n);
        p.processBlock(b, midi);
        const float* ins[]{x[0].data(), x[1].data()}; float* outs[]{y[0].data(), y[1].data()};
        direct.process(ins, outs, unsigned(n));
        const auto taken = p.takeOutputPeaks();
        for (int c = 0; c < 2; ++c) {
            if (std::memcmp(b.getReadPointer(c), y[c].data(), size_t(n) * sizeof(float)) != 0)
                return fail(40, "hosted hot-input output differs from core");
            const float actual = b.getMagnitude(c, 0, n);
            if (taken[size_t(c)] != actual) return fail(41, "meter tap differs from actual output peak");
            held[c] = std::max(held[c], taken[size_t(c)]); hostedPeak = std::max(hostedPeak, actual);
        }
    }
    if (std::abs(hostedPeak - 0.0662401095f) > 1e-6f) return fail(42, "hosted hot-input peak differs from genuine");
    std::cout << "hot input: hosted peak " << juce::Decibels::gainToDecibels(hostedPeak) << " dBFS, meter hold L/R "
              << juce::Decibels::gainToDecibels(held[0]) << '/' << juce::Decibels::gainToDecibels(held[1]) << " dBFS\n";
    return 0;
}
// Opt-in mono: 1-in/1-out hosted layout drives a 1-channel core bit-identically,
// keeps state/parameters, publishes the core meters; unsupported layouts are refused.
int monoLayout() {
    using Set = juce::AudioChannelSet;
    tsunami::Adapter<Spec> p;
    auto layout = [](Set in, Set out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add(in); l.outputBuses.add(out); return l; };
    if (!p.checkBusesLayoutSupported(layout(Set::mono(), Set::mono()))) return fail(60, "mono 1-in/1-out not offered");
    if (!p.checkBusesLayoutSupported(layout(Set::stereo(), Set::stereo()))) return fail(61, "stereo layout lost");
    if (p.checkBusesLayoutSupported(layout(Set::mono(), Set::stereo())) || p.checkBusesLayoutSupported(layout(Set::stereo(), Set::mono()))
        || p.checkBusesLayoutSupported(layout(Set::create5point1(), Set::create5point1())))
        return fail(62, "mismatched or surround layout accepted");
    if (!p.setBusesLayout(layout(Set::mono(), Set::mono())) || p.getTotalNumOutputChannels() != 1) return fail(63, "host could not select mono");
    const auto& params = p.getParameters();
    setPhysical(params[0], -24.f); setPhysical(params[1], -60.f); setPhysical(params[2], -2.f);
    const double rate = 48000; const int block = 96;
    p.prepareToPlay(rate, block);
    Spec::Core direct;
    for (int i = 0; i < 3; ++i) direct.setParameter(unsigned(i), physical(params[i]));
    direct.prepare(rate, 1, unsigned(block)); direct.reset();
    if (p.getLatencySamples() != int(direct.latencySamples())) return fail(64, "mono latency differs from core");
    const unsigned frames = unsigned(rate);
    const auto x = program(frames, rate, 0);
    std::vector<float> ref(frames), out(frames); juce::MidiBuffer midi;
    auto* meters = dynamic_cast<tsunami::CoreMeterSource*>(&p);
    if (!meters || meters->coreMeterCount() != Spec::Core::meterCount) return fail(65, "mono adapter lacks core meter bridge");
    for (unsigned at = 0; at < frames; at += unsigned(block)) {
        const unsigned n = std::min(unsigned(block), frames - at);
        juce::AudioBuffer<float> b(1, int(n));
        std::memcpy(b.getWritePointer(0), x.data() + at, n * sizeof(float));
        p.processBlock(b, midi);
        std::memcpy(out.data() + at, b.getReadPointer(0), n * sizeof(float));
        const float* in[]{x.data() + at}; float* o[]{ref.data() + at};
        direct.process(in, o, n);
        for (unsigned m = 0; m < Spec::Core::meterCount; ++m)
            if (meters->coreMeter(m) != direct.meter(m)) return fail(66, "mono bridge meter differs from core");
    }
    if (std::memcmp(out.data(), ref.data(), frames * sizeof(float)) != 0) return fail(67, "mono hosted output differs from 1-channel core");
    if (!(levelSpread(out, rate) < levelSpread(x, rate) - 3) || !(meters->coreMeter(1) < .5f)) return fail(68, "mono path not compressing");
    juce::MemoryBlock saved; p.getStateInformation(saved);
    tsunami::Adapter<Spec> fresh;
    if (!fresh.setBusesLayout(layout(Set::mono(), Set::mono()))) return fail(71, "fresh mono layout");
    fresh.setStateInformation(saved.getData(), int(saved.getSize()));
    const auto& fp = fresh.getParameters();
    if (std::abs(physical(fp[0]) + 24) > 1e-3 || std::abs(physical(fp[1]) + 60) > 1e-3 || std::abs(physical(fp[2]) + 2) > 1e-3)
        return fail(69, "mono state restore lost controls");
    juce::AudioBuffer<float> wrong(2, block); wrong.clear(); wrong.setSample(0, 0, 1.f); wrong.setSample(1, 0, 1.f);
    p.processBlock(wrong, midi); // a stereo buffer while prepared mono is silenced, never misread
    if (wrong.getMagnitude(0, block) != 0.f) return fail(70, "mismatched channel count not silenced");
    std::cout << "rvox.adapter mono PASS: 1-in/1-out bit-identical to 1-channel core, meters, state, layout refusal\n";
    return 0;
}
// Host bypass (VST3/LV2 wrappers call processBlockBypassed when the plugin has no
// bypass parameter) must keep the reported lookahead latency, otherwise host delay
// compensation shifts the bypassed audio earlier. Nonperiodic noise so a lag is unique.
std::vector<float> noise(unsigned frames, uint32_t seed) {
    std::vector<float> x(frames);
    for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = float(int32_t(seed) >> 8) / float(1 << 23) * .5f; }
    return x;
}
// Lag (0..maxLag) maximizing correlation of y[at..at+len) against x shifted by lag.
int bestLag(const std::vector<float>& x, const std::vector<float>& y, unsigned at, unsigned len, int maxLag) {
    int best = -1; double top = -1e30;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double s = 0; for (unsigned n = at; n < at + len; ++n) s += double(y[n]) * x[n - unsigned(lag)];
        if (s > top) { top = s; best = lag; }
    }
    return best;
}
int bypassLatency() {
    using Set = juce::AudioChannelSet;
    for (int chans : {1, 2}) {
        tsunami::Adapter<Spec> p;
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add(chans == 1 ? Set::mono() : Set::stereo()); l.outputBuses.add(l.inputBuses[0]);
        if (!p.setBusesLayout(l)) return fail(80, "bypass layout");
        const int block = 64; const unsigned frames = 48000, toggle = 24000 - 17; // toggle mid-block stream position
        for (int round = 0; round < 2; ++round) { // second round: repeated prepare + reset must restart cleanly
            p.prepareToPlay(48000, block); p.reset();
            const int latency = p.getLatencySamples();
            if (latency <= 0) return fail(81, "RVox reports no lookahead");
            std::vector<float> in[2]{noise(frames, 7u), noise(frames, 99u)}, out[2]{std::vector<float>(frames), std::vector<float>(frames)};
            // Pure bypass from a cold start: exact copy of input delayed by getLatencySamples().
            juce::MidiBuffer midi; juce::AudioBuffer<float> b(chans, block);
            for (unsigned at = 0; at < frames; at += unsigned(block)) {
                for (int c = 0; c < chans; ++c) b.copyFrom(c, 0, in[c].data() + at, block);
                countAllocations = true; p.processBlockBypassed(b, midi); countAllocations = false;
                for (int c = 0; c < chans; ++c) std::memcpy(out[c].data() + at, b.getReadPointer(c), size_t(block) * sizeof(float));
            }
            const int lag = bestLag(in[0], out[0], 4800, 9600, 256);
            std::cout << "bypass " << chans << "ch round " << round << ": reported latency " << latency << ", measured bypass delay " << lag << '\n';
            for (int c = 0; c < chans; ++c)
                for (unsigned n = 0; n < frames; ++n)
                    if (out[c][n] != (n >= unsigned(latency) ? in[c][n - unsigned(latency)] : 0.f))
                        return fail(82, "bypass output is not the input delayed by the reported latency");
            // Continuous stream: neutral/default processing, then host bypass from `toggle`.
            p.reset();
            for (unsigned at = 0, k = 0; at < frames; ++k) {
                const unsigned n = std::min({unsigned(block), frames - at, at < toggle && at + unsigned(block) > toggle ? toggle - at : unsigned(block)});
                juce::AudioBuffer<float> s(chans, int(n));
                for (int c = 0; c < chans; ++c) s.copyFrom(c, 0, in[c].data() + at, int(n));
                countAllocations = true;
                if (at < toggle) p.processBlock(s, midi); else p.processBlockBypassed(s, midi);
                countAllocations = false;
                for (int c = 0; c < chans; ++c) std::memcpy(out[c].data() + at, s.getReadPointer(c), n * sizeof(float));
                at += n;
            }
            const int before = bestLag(in[0], out[0], toggle - 4800, 4800, 256), after = bestLag(in[0], out[0], toggle + 512, 4800, 256);
            std::cout << "  toggle processing->bypass: delay before " << before << ", after " << after << '\n';
            if (before != latency || after != latency) return fail(83, "bypass toggle jumps in time (not just amplitude)");
            for (int c = 0; c < chans; ++c) // already-aligned history: exact delayed input right at the toggle
                for (unsigned n = toggle; n < frames; ++n)
                    if (out[c][n] != in[c][n - unsigned(latency)]) return fail(84, "bypass after toggle lacks aligned dry history");
        }
    }
    if (allocations != 0) return fail(85, "warm audio callbacks allocated");
    countAllocations = true; ::operator delete(::operator new(4)); countAllocations = false; // counter control
    if (allocations != 1) return fail(86, "allocation counter is not observing operator new");
    // Probe only (reported, not asserted here): an invalid host rate reaching prepareToPlay.
    for (double bad : {0.0, -1.0, std::nan("")}) {
        tsunami::Adapter<Spec> q;
        try { q.prepareToPlay(bad, 64); std::cout << "probe prepare rate " << bad << ": no exception\n"; }
        catch (const std::exception& e) { std::cout << "probe prepare rate " << bad << ": exception escapes prepareToPlay: " << e.what() << '\n'; }
    }
    std::cout << "rvox.adapter bypass PASS: host bypass delayed by reported latency, mono/stereo, toggle aligned, reset/re-prepare\n";
    return 0;
}
// Genuine-comparison mode: recall a preset through saved state into a fresh
// hosted instance, render interleaved stereo float32 at the original block size.
int presetRender(char** a) {
    tsunami::Adapter<Spec> author; const auto& ap = author.getParameters();
    for (int i = 0; i < 3; ++i) setPhysical(ap[i], std::stof(a[5 + i]));
    juce::MemoryBlock preset; author.getStateInformation(preset);
    tsunami::Adapter<Spec> p; p.setStateInformation(preset.getData(), int(preset.getSize()));
    const double rate = std::stod(a[3]); const int block = std::stoi(a[4]);
    p.prepareToPlay(rate, block);
    juce::File src(a[1]); juce::MemoryBlock raw; if (!src.loadFileAsData(raw) || raw.getSize() % 8) return 4;
    const unsigned frames = unsigned(raw.getSize() / 8), latency = unsigned(p.getLatencySamples());
    const float* x = static_cast<const float*>(raw.getData());
    std::vector<float> out(size_t(frames) * 2); juce::MidiBuffer midi;
    for (unsigned at = 0; at < frames + latency; at += unsigned(block)) {
        const unsigned n = std::min(unsigned(block), frames + latency - at);
        juce::AudioBuffer<float> b(2, int(n)); b.clear();
        for (unsigned i = 0; i < n; ++i) if (at + i < frames) for (int c = 0; c < 2; ++c) b.setSample(c, int(i), x[(at + i) * 2 + unsigned(c)]);
        p.processBlock(b, midi);
        for (unsigned i = 0; i < n; ++i) if (at + i >= latency) for (int c = 0; c < 2; ++c) out[(at + i - latency) * 2 + unsigned(c)] = b.getSample(c, int(i));
    }
    return juce::File(a[2]).replaceWithData(out.data(), out.size() * sizeof(float)) ? 0 : 5;
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    // Never read or write the real user skin preference: point it at a temp file first.
    struct TempPref {
        juce::File dir = juce::File::createTempFile("rvox-adapter-skin");
        TempPref() { dir.createDirectory(); }
        ~TempPref() { dir.deleteRecursively(); }
    } tempPref;
    const auto realPref = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Tsunami").getChildFile("RVox").getChildFile("skin.txt");
    const bool realExisted = realPref.exists();
    const auto realModified = realPref.getLastModificationTime();
    const auto testPref = tempPref.dir.getChildFile("skin.txt");
    testPref.replaceWithText("dark");
    tsunami::rvox::SkinPreference::location() = testPref;
    // rvox_adapter_test in.raw out.raw rate block compression gate gain
    if (argc == 8) return presetRender(argv);
    if (std::strcmp(Spec::id, "rvox") != 0) return fail(2, "hosted product is not RVox");
    if (std::strcmp(Spec::name, "Tsunami Voice") != 0) return fail(2, "product identity is not targets.json 'Tsunami Voice'");
    tsunami::Adapter<Spec> p;
    if (p.getName() != "Tsunami Voice") return fail(2, "hosted name is not 'Tsunami Voice'");
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
    {
        auto* skin = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("skin"));
        if (!skin || skin->getText() != "Dark" || testPref.loadFileAsString() != "dark" ||
            realPref.exists() != realExisted || realPref.getLastModificationTime() != realModified)
            return fail(9, "editor must read only the isolated test skin preference");
    }
    if (int r = hiddenStateMatrix()) return r;
    if (int r = hotInputMeter()) return r;
    if (int r = monoLayout()) return r;
    if (int r = bypassLatency()) return r;
    std::cout << "rvox.adapter PASS: hosted RVox core bit-identical across 3 rates x 3 blocks, state restore exact\n";
    return 0;
}
