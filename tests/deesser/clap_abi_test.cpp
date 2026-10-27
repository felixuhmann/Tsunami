// SPDX-License-Identifier: GPL-3.0-or-later
// Thin DeEsser CLAP ABI harness: loads the REAL compiled product .clap module and drives
// clap.audio-ports-config, clap.audio-ports, params, state and process through the pinned CLAP ABI.
// Output is compared against the portable tsunami::deesser::Processor at the same physical values.
// Layout regression only; not a screenshot host and not an original-reference comparison.
// usage: deesser_clap_abi_test <Tsunami De-Esser.clap>
#include <clap/clap.h>
#include "Processor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define REQUIRE(c) do { if (!(c)) { std::fprintf(stderr, "FATAL %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

namespace {
constexpr const char* pluginId = "org.tsunami.deesser";
constexpr double rate = 48000.0;
constexpr uint32_t maxFrames = 512;
// Same float-rounding budget class as the hosted Adapter-vs-core comparison; the paths share one core.
constexpr double tolerance = 1e-5;

const void* hostExtension(const clap_host*, const char*) { return nullptr; }
void hostRequest(const clap_host*) {}
const clap_host host{CLAP_VERSION_INIT, nullptr, "Tsunami DeEsser layout harness", "Tsunami", "", "0",
                     hostExtension, hostRequest, hostRequest, hostRequest};

struct Module {
    const clap_plugin_entry* entry = nullptr;
    explicit Module(const char* path) {
#ifdef _WIN32
        auto* lib = LoadLibraryA(path);
        REQUIRE(lib != nullptr);
        entry = reinterpret_cast<const clap_plugin_entry*>(GetProcAddress(lib, "clap_entry"));
#else
        void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!lib) std::fprintf(stderr, "%s\n", dlerror());
        REQUIRE(lib != nullptr);
        entry = static_cast<const clap_plugin_entry*>(dlsym(lib, "clap_entry"));
#endif
        REQUIRE(entry != nullptr && clap_version_is_compatible(entry->clap_version));
        REQUIRE(entry->init(path));
    }
    const clap_plugin* create() const {
        auto* factory = static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        REQUIRE(factory != nullptr);
        const clap_plugin* plugin = factory->create_plugin(factory, &host, pluginId);
        REQUIRE(plugin != nullptr && plugin->init(plugin));
        return plugin;
    }
};

template<class T> const T* ext(const clap_plugin* p, const char* id) { return static_cast<const T*>(p->get_extension(p, id)); }

void expectPorts(const clap_plugin* p, uint32_t channels) {
    auto* ports = ext<clap_plugin_audio_ports>(p, CLAP_EXT_AUDIO_PORTS);
    REQUIRE(ports != nullptr);
    REQUIRE(ports->count(p, true) == 1 && ports->count(p, false) == 1);
    clap_audio_port_info in{}, out{};
    REQUIRE(ports->get(p, 0, true, &in) && ports->get(p, 0, false, &out));
    const char* type = channels == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    CHECK(in.channel_count == channels && out.channel_count == channels);
    CHECK(in.port_type && std::strcmp(in.port_type, type) == 0);
    CHECK(out.port_type && std::strcmp(out.port_type, type) == 0);
    CHECK(in.in_place_pair == out.id && out.in_place_pair == in.id && in.id != out.id);
    CHECK((in.flags & CLAP_AUDIO_PORT_IS_MAIN) && (out.flags & CLAP_AUDIO_PORT_IS_MAIN));
}

// Product parameters by display name with their physical range (parameters.json order = core index).
struct Param { const char* name; float min, max; clap_id id = CLAP_INVALID_ID; double lo = 0, hi = 0; };
Param params[] = {{"Freq", 2000, 16000}, {"Threshold", -80, 0}, {"SideChain", 0, 1},
                  {"Audio", 0, 1}, {"Monitor", 0, 1}, {"Bypass", 0, 1}};
constexpr unsigned freq = 0, threshold = 1, sidechain = 2, audio = 3, monitor = 4, bypass = 5;

void bindParams(const clap_plugin* p) {
    auto* ext_ = ext<clap_plugin_params>(p, CLAP_EXT_PARAMS);
    REQUIRE(ext_ != nullptr);
    const uint32_t count = ext_->count(p);
    std::printf("params: %u\n", count);
    REQUIRE(count >= 6); // the real product, not the 1-parameter fixture
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info info{};
        REQUIRE(ext_->get_info(p, i, &info));
        for (auto& q : params)
            if (std::strcmp(info.name, q.name) == 0) { q.id = info.id; q.lo = info.min_value; q.hi = info.max_value; }
    }
    for (auto& q : params) { REQUIRE(q.id != CLAP_INVALID_ID && q.hi > q.lo); }
}
// Linear physical -> CLAP value; used only for linear Threshold and the two-state choices/bool.
double clapValue(unsigned i, float physical) {
    const auto& q = params[i];
    return q.lo + double(physical - q.min) / double(q.max - q.min) * (q.hi - q.lo);
}
double readValue(const clap_plugin* p, unsigned i) {
    double v = -1;
    REQUIRE(ext<clap_plugin_params>(p, CLAP_EXT_PARAMS)->get_value(p, params[i].id, &v));
    return v;
}

struct Events { clap_input_events list; std::vector<clap_event_param_value> events; };
uint32_t eventsSize(const clap_input_events* l) { return uint32_t(static_cast<const Events*>(l->ctx)->events.size()); }
const clap_event_header* eventsGet(const clap_input_events* l, uint32_t i) {
    auto* e = static_cast<const Events*>(l->ctx);
    return i < e->events.size() ? &e->events[i].header : nullptr;
}
bool dropEvent(const clap_output_events*, const clap_event_header*) { return true; }

// Named active settings, all physical (Freq stays at its exact whole-Hz default 5506).
struct Setting { const char* name; float threshold, sidechain, audio, monitor, bypass; bool reduces, neutral; };
const Setting settings[] = {
    {"split-highpass-th-40", -40.f, 1.f, 0.f, 0.f, 0.f, true, false},
    {"wideband-bandpass-th-30", -30.f, 0.f, 1.f, 0.f, 0.f, true, false},
    {"bypass-neutral", -40.f, 1.f, 0.f, 0.f, 1.f, false, true},
};

float signal(uint32_t c, uint64_t n) {
    // Sibilant 7 kHz band plus channel-distinct low content; channel 1 differs in phase and level.
    const double t = double(n) / rate;
    return c == 0 ? float(0.45 * std::sin(2 * 3.14159265358979 * 7000 * t) + 0.2 * std::sin(2 * 3.14159265358979 * 220 * t))
                  : float(-0.25 * std::cos(2 * 3.14159265358979 * 7300 * t) + 0.3 * std::sin(2 * 3.14159265358979 * 330 * t));
}

// Processes several blocks through the CLAP module and the direct core; sentinel guards on all buffers.
void processCompare(const clap_plugin* p, uint32_t channels, const Setting& s) {
    tsunami::deesser::Processor reference;
    reference.prepare(rate, channels, maxFrames);
    reference.setParameter(threshold, s.threshold); reference.setParameter(sidechain, s.sidechain);
    reference.setParameter(audio, s.audio); reference.setParameter(monitor, s.monitor); reference.setParameter(bypass, s.bypass);
    reference.reset();
    REQUIRE(p->activate(p, rate, 1, maxFrames));
    REQUIRE(p->start_processing(p));
    p->reset(p);
    constexpr uint32_t guard = 16;
    constexpr float sentinel = 1234.5f;
    double inEnergy = 0, outEnergy = 0, maxDiff = 0, inOutDiff = 0;
    bool distinct = false;
    uint64_t t = 0;
    const uint32_t sizes[] = {256, 512, 512, 512, 512, 333};
    for (unsigned b = 0; b < sizeof sizes / sizeof sizes[0]; ++b) {
        const uint32_t frames = sizes[b];
        std::vector<std::vector<float>> in(channels, std::vector<float>(frames + 2 * guard, sentinel));
        std::vector<std::vector<float>> out(channels, std::vector<float>(frames + 2 * guard, sentinel));
        std::vector<std::vector<float>> ref(channels, std::vector<float>(frames));
        std::vector<float*> inPtr, outPtr, refPtr;
        for (uint32_t c = 0; c < channels; ++c) {
            for (uint32_t n = 0; n < frames; ++n) ref[c][n] = in[c][guard + n] = signal(c, t + n);
            inPtr.push_back(in[c].data() + guard); outPtr.push_back(out[c].data() + guard); refPtr.push_back(ref[c].data());
        }
        Events events{};
        events.list = {&events, eventsSize, eventsGet};
        if (b == 0) { // automate every non-default control at sample 0 of the first block
            const std::pair<unsigned, float> values[] = {{threshold, s.threshold}, {sidechain, s.sidechain},
                                                         {audio, s.audio}, {monitor, s.monitor}, {bypass, s.bypass}};
            for (const auto& [i, v] : values) {
                clap_event_param_value e{};
                e.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                e.param_id = params[i].id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = clapValue(i, v);
                events.events.push_back(e);
            }
        }
        clap_output_events outEvents{nullptr, dropEvent};
        clap_audio_buffer inBuf{inPtr.data(), nullptr, channels, 0, 0}, outBuf{outPtr.data(), nullptr, channels, 0, 0};
        clap_process proc{int64_t(t), frames, nullptr, &inBuf, &outBuf, 1, 1, &events.list, &outEvents};
        CHECK(p->process(p, &proc) != CLAP_PROCESS_ERROR);
        reference.process(refPtr.data(), refPtr.data(), frames);
        for (uint32_t c = 0; c < channels; ++c) {
            for (uint32_t g = 0; g < guard; ++g) {
                CHECK(in[c][g] == sentinel && in[c][guard + frames + g] == sentinel);
                CHECK(out[c][g] == sentinel && out[c][guard + frames + g] == sentinel);
            }
            for (uint32_t n = 0; n < frames; ++n) {
                const float y = out[c][guard + n], x = signal(c, t + n);
                CHECK(std::isfinite(y));
                maxDiff = std::max(maxDiff, double(std::abs(y - ref[c][n])));
                inOutDiff = std::max(inOutDiff, double(std::abs(y - x)));
                if (b > 0) { inEnergy += double(x) * x; outEnergy += double(y) * y; } // skip the attack block
                if (channels == 2 && c == 1) distinct = distinct || y != out[0][guard + n];
            }
        }
        t += frames;
    }
    const double ratio = outEnergy / inEnergy;
    std::printf("  %u ch %-24s maxDiff(core)=%.3g energyRatio=%.4f maxDiff(input)=%.3g\n", channels, s.name, maxDiff, ratio, inOutDiff);
    CHECK(maxDiff <= tolerance);
    CHECK(inEnergy > 10.0 && outEnergy > 1.0);
    if (s.reduces) CHECK(ratio < 0.9);             // meaningful de-essing, not a pass-through
    if (s.neutral) CHECK(inOutDiff <= tolerance);  // neutral control: Bypass returns the input
    if (channels == 2) CHECK(distinct);
    CHECK(std::abs(readValue(p, threshold) - clapValue(threshold, s.threshold)) < 1e-6);
    CHECK(std::abs(readValue(p, audio) - clapValue(audio, s.audio)) < 1e-6);
    CHECK(std::abs(readValue(p, bypass) - clapValue(bypass, s.bypass)) < 1e-6);
    p->stop_processing(p);
}

void runAll(const clap_plugin* p, uint32_t channels) {
    for (const auto& s : settings) { processCompare(p, channels, s); p->deactivate(p); expectPorts(p, channels); }
}

struct Stream { std::vector<char> bytes; size_t at = 0; };
int64_t writeStream(const clap_ostream* s, const void* data, uint64_t size) {
    auto* st = static_cast<Stream*>(s->ctx);
    st->bytes.insert(st->bytes.end(), static_cast<const char*>(data), static_cast<const char*>(data) + size);
    return int64_t(size);
}
int64_t readStream(const clap_istream* s, void* data, uint64_t size) {
    auto* st = static_cast<Stream*>(s->ctx);
    const size_t n = std::min<size_t>(size_t(size), st->bytes.size() - st->at);
    std::memcpy(data, st->bytes.data() + st->at, n); st->at += n;
    return int64_t(n);
}
void stateRoundtrip(const Module& module, const clap_plugin* p) {
    const double th = readValue(p, threshold), au = readValue(p, audio), by = readValue(p, bypass), fr = readValue(p, freq);
    Stream stream;
    clap_ostream os{&stream, writeStream};
    REQUIRE(ext<clap_plugin_state>(p, CLAP_EXT_STATE)->save(p, &os) && !stream.bytes.empty());
    const clap_plugin* fresh = module.create();
    CHECK(std::abs(readValue(fresh, threshold) - th) > 1e-3); // fresh default (0 dB) differs from -40 dB
    clap_istream is{&stream, readStream};
    REQUIRE(ext<clap_plugin_state>(fresh, CLAP_EXT_STATE)->load(fresh, &is));
    CHECK(std::abs(readValue(fresh, threshold) - th) < 1e-6 && std::abs(readValue(fresh, audio) - au) < 1e-6);
    CHECK(std::abs(readValue(fresh, bypass) - by) < 1e-6 && std::abs(readValue(fresh, freq) - fr) < 1e-6);
    expectPorts(fresh, 2); // the selected config is not part of plugin state (SDK limitation)
    fresh->destroy(fresh);
}
}

int main(int argc, char** argv) {
    REQUIRE(argc == 2);
    Module module(argv[1]);
    const clap_plugin* p = module.create();
    bindParams(p);
    expectPorts(p, 2); // default layout is stereo
    auto* configs = ext<clap_plugin_audio_ports_config>(p, CLAP_EXT_AUDIO_PORTS_CONFIG);
    REQUIRE(configs != nullptr); // product opts into mono (parameters.json supports_mono)
    REQUIRE(configs->count(p) == 2);
    clap_audio_ports_config a{}, b{}, extra{};
    REQUIRE(configs->get(p, 0, &a) && configs->get(p, 1, &b));
    CHECK(!configs->get(p, 2, &extra));
    CHECK(a.id != b.id && a.id != CLAP_INVALID_ID && b.id != CLAP_INVALID_ID);
    CHECK(a.main_input_channel_count == 2); // index 0 is the stereo default
    const auto& mono = a.main_input_channel_count == 1 ? a : b;
    const auto& stereo = a.main_input_channel_count == 1 ? b : a;
    for (const auto* c : {&mono, &stereo}) {
        const uint32_t n = c == &mono ? 1u : 2u;
        const char* type = n == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
        CHECK(c->input_port_count == 1 && c->output_port_count == 1 && c->has_main_input && c->has_main_output);
        CHECK(c->main_input_channel_count == n && c->main_output_channel_count == n);
        CHECK(c->main_input_port_type && std::strcmp(c->main_input_port_type, type) == 0);
        CHECK(c->main_output_port_type && std::strcmp(c->main_output_port_type, type) == 0);
        CHECK(std::strlen(c->name) > 0);
    }
    { // stable IDs across queries and instances
        const clap_plugin* other = module.create();
        auto* otherConfigs = ext<clap_plugin_audio_ports_config>(other, CLAP_EXT_AUDIO_PORTS_CONFIG);
        clap_audio_ports_config again{};
        CHECK(otherConfigs && otherConfigs->count(other) == 2 && otherConfigs->get(other, 0, &again) && again.id == a.id);
        other->destroy(other);
    }
    CHECK(!configs->select(p, CLAP_INVALID_ID));
    CHECK(!configs->select(p, a.id + b.id + 100));
    expectPorts(p, 2);

    REQUIRE(configs->select(p, mono.id));
    expectPorts(p, 1);
    processCompare(p, 1, settings[0]); // active: reconfiguration must be rejected and leave mono in place
    CHECK(!configs->select(p, stereo.id));
    expectPorts(p, 1);
    p->deactivate(p);
    runAll(p, 1);

    REQUIRE(configs->select(p, stereo.id));
    expectPorts(p, 2);
    processCompare(p, 2, settings[1]);
    CHECK(!configs->select(p, mono.id));
    expectPorts(p, 2);
    p->deactivate(p);
    runAll(p, 2);

    REQUIRE(configs->select(p, mono.id)); // reselect and re-activate
    processCompare(p, 1, settings[0]);
    p->deactivate(p);
    expectPorts(p, 1);
    stateRoundtrip(module, p);
    p->destroy(p);
    module.entry->deinit();
    std::printf("PASS-GATE %s: %s (%d failures)\n", pluginId, failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
