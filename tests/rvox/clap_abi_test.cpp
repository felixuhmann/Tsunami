// SPDX-License-Identifier: GPL-3.0-or-later
// RVox CLAP layout regression: loads the REAL compiled "Tsunami Voice.clap" through the CLAP ABI,
// selects host mono/stereo audio-ports configs and checks the module's audio against a directly
// driven portable RVox core at the same physical settings. Layout/ABI check only, not a UI host.
// usage: rvox_clap_abi_test <Tsunami Voice.clap>
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
constexpr const char* pluginId = "org.tsunami.rvox";
constexpr double rate = 48000.0;
constexpr uint32_t block = 512;
constexpr double pi = 3.14159265358979323846;
// Named active setting (physical units from plugins/rvox/parameters.json): heavy compression,
// gate open threshold and output trim, as in the hosted adapter mono test.
const char* const names[3] = {"Compression", "Gate", "Gain"};
const float lowest[3] = {-36.f, -80.f, -36.f}, highest[3] = {0.f, 0.f, 0.f};
const float active[3] = {-24.f, -60.f, -2.f};
const float defaults[3] = {0.f, -80.f, 0.f};

const void* hostExtension(const clap_host*, const char*) { return nullptr; }
void hostRequest(const clap_host*) {}
const clap_host host{CLAP_VERSION_INIT, nullptr, "Tsunami RVox CLAP ABI harness", "Tsunami", "", "0",
                     hostExtension, hostRequest, hostRequest, hostRequest};

struct Events { clap_input_events list; std::vector<clap_event_param_value> items; };
uint32_t eventsSize(const clap_input_events* l) { return uint32_t(static_cast<const Events*>(l->ctx)->items.size()); }
const clap_event_header* eventsGet(const clap_input_events* l, uint32_t i) {
    auto* e = static_cast<const Events*>(l->ctx);
    return i < e->items.size() ? &e->items[i].header : nullptr;
}
bool dropEvent(const clap_output_events*, const clap_event_header*) { return true; }

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
    REQUIRE(ports != nullptr && ports->count(p, true) == 1 && ports->count(p, false) == 1);
    clap_audio_port_info in{}, out{};
    REQUIRE(ports->get(p, 0, true, &in) && ports->get(p, 0, false, &out));
    const char* type = channels == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    CHECK(in.channel_count == channels && out.channel_count == channels);
    CHECK(in.port_type && std::strcmp(in.port_type, type) == 0);
    CHECK(out.port_type && std::strcmp(out.port_type, type) == 0);
    CHECK((in.flags & CLAP_AUDIO_PORT_IS_MAIN) && (out.flags & CLAP_AUDIO_PORT_IS_MAIN));
}

// The three RVox controls located by name (ids are wrapper hashes); CLAP value <-> physical units.
struct Controls {
    clap_param_info info[3]{};
    explicit Controls(const clap_plugin* p) {
        auto* params = ext<clap_plugin_params>(p, CLAP_EXT_PARAMS);
        REQUIRE(params != nullptr);
        const uint32_t count = params->count(p);
        CHECK(count == 3);
        bool found[3]{};
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info pi{};
            REQUIRE(params->get_info(p, i, &pi));
            for (int k = 0; k < 3; ++k)
                if (std::strcmp(pi.name, names[k]) == 0) { info[k] = pi; found[k] = true; }
        }
        for (int k = 0; k < 3; ++k) REQUIRE(found[k] && info[k].max_value > info[k].min_value);
    }
    double toClap(int k, float physical) const {
        return info[k].min_value + (double(physical) - lowest[k]) / (highest[k] - lowest[k]) * (info[k].max_value - info[k].min_value);
    }
    double physical(const clap_plugin* p, int k) const {
        double v = 0;
        REQUIRE(ext<clap_plugin_params>(p, CLAP_EXT_PARAMS)->get_value(p, info[k].id, &v));
        return lowest[k] + (v - info[k].min_value) / (info[k].max_value - info[k].min_value) * (highest[k] - lowest[k]);
    }
};

std::vector<float> program(uint32_t frames, uint32_t channel) {
    std::vector<float> x(frames);
    for (uint32_t n = 0; n < frames; ++n) {
        const double t = n / rate;
        const double envelope = .05 + .85 * (0.5 + 0.5 * std::sin(2 * pi * 3.0 * t));
        x[n] = float(envelope * std::sin(2 * pi * (220.0 + 110 * channel) * t));
    }
    return x;
}
// Spread (dB) of 50 ms RMS levels after the first quarter second.
double levelSpread(const std::vector<float>& x) {
    const uint32_t window = uint32_t(rate / 20);
    double lo = 1e9, hi = -1e9;
    for (uint32_t at = uint32_t(rate / 4); at + window <= x.size(); at += window) {
        double s = 0; for (uint32_t n = at; n < at + window; ++n) s += double(x[n]) * x[n];
        const double db = 10 * std::log10(s / window + 1e-30);
        lo = std::min(lo, db); hi = std::max(hi, db);
    }
    return hi - lo;
}

// One second of channel-distinct program through the module at the named active setting (sent as
// sample-0 parameter events), compared in lockstep with the direct core at the read-back values.
void processActive(const clap_plugin* p, uint32_t channels, int64_t& steady) {
    const Controls controls(p);
    REQUIRE(p->activate(p, rate, 1, block));
    REQUIRE(p->start_processing(p));
    const uint32_t frames = uint32_t(rate);
    std::vector<std::vector<float>> x, y(channels, std::vector<float>(frames)), ref(channels, std::vector<float>(frames));
    for (uint32_t c = 0; c < channels; ++c) x.push_back(program(frames, c));
    tsunami::rvox::Processor direct;
    constexpr uint32_t guard = 16;
    constexpr float sentinel = 1234.5f;
    clap_output_events outEvents{nullptr, dropEvent};
    for (uint32_t at = 0; at < frames; at += block) {
        const uint32_t n = std::min(block, frames - at);
        Events events{};
        events.list = {&events, eventsSize, eventsGet};
        if (at == 0)
            for (int k = 0; k < 3; ++k) {
                clap_event_param_value e{};
                e.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                e.param_id = controls.info[k].id;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = controls.toClap(k, active[k]);
                events.items.push_back(e);
            }
        std::vector<std::vector<float>> in(channels, std::vector<float>(n + 2 * guard, sentinel)), out = in;
        std::vector<float*> inPtr, outPtr;
        for (uint32_t c = 0; c < channels; ++c) {
            std::memcpy(in[c].data() + guard, x[c].data() + at, n * sizeof(float));
            inPtr.push_back(in[c].data() + guard);
            outPtr.push_back(out[c].data() + guard);
        }
        clap_audio_buffer inBuf{inPtr.data(), nullptr, channels, 0, 0}, outBuf{outPtr.data(), nullptr, channels, 0, 0};
        clap_process proc{steady, n, nullptr, &inBuf, &outBuf, 1, 1, &events.list, &outEvents};
        CHECK(p->process(p, &proc) != CLAP_PROCESS_ERROR);
        steady += n;
        if (at == 0) { // the core gets exactly the physical values the module reports back
            for (int k = 0; k < 3; ++k) {
                const double v = controls.physical(p, k);
                CHECK(std::abs(v - active[k]) < 1e-3);
                direct.setParameter(unsigned(k), float(v));
            }
            direct.prepare(rate, channels, block); direct.reset();
        }
        std::vector<const float*> refIn; std::vector<float*> refOut;
        for (uint32_t c = 0; c < channels; ++c) { refIn.push_back(x[c].data() + at); refOut.push_back(ref[c].data() + at); }
        direct.process(refIn.data(), refOut.data(), n);
        for (uint32_t c = 0; c < channels; ++c) {
            for (uint32_t g = 0; g < guard; ++g) {
                CHECK(in[c][g] == sentinel && in[c][guard + n + g] == sentinel);
                CHECK(out[c][g] == sentinel && out[c][guard + n + g] == sentinel);
            }
            std::memcpy(y[c].data() + at, out[c].data() + guard, n * sizeof(float));
        }
    }
    p->stop_processing(p);
    double maxDiff = 0, energy = 0;
    for (uint32_t c = 0; c < channels; ++c)
        for (uint32_t n = 0; n < frames; ++n) {
            CHECK(std::isfinite(y[c][n]));
            maxDiff = std::max(maxDiff, double(std::abs(y[c][n] - ref[c][n])));
            energy += double(y[c][n]) * y[c][n];
        }
    // Same float-rounding tolerance as the SDK CLAP layout harness: the module is the core.
    CHECK(maxDiff <= 1e-6);
    CHECK(energy > 1.0);
    for (uint32_t c = 0; c < channels; ++c) // meaningful effect: -24 dB compression flattens the level
        CHECK(levelSpread(y[c]) < levelSpread(x[c]) - 3);
    if (channels == 2) CHECK(y[0] != y[1]);
    std::printf("%u-channel CLAP vs direct core: max |diff| %.3g, spread in %.2f dB -> out %.2f dB\n",
                channels, maxDiff, levelSpread(x[0]), levelSpread(y[0]));
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
    const Controls controls(p);
    for (int k = 0; k < 3; ++k) CHECK(std::abs(controls.physical(p, k) - active[k]) < 1e-3);
    Stream stream;
    clap_ostream os{&stream, writeStream};
    REQUIRE(ext<clap_plugin_state>(p, CLAP_EXT_STATE)->save(p, &os) && !stream.bytes.empty());
    const clap_plugin* fresh = module.create();
    for (int k = 0; k < 3; ++k) CHECK(std::abs(controls.physical(fresh, k) - defaults[k]) < 1e-3);
    clap_istream is{&stream, readStream};
    REQUIRE(ext<clap_plugin_state>(fresh, CLAP_EXT_STATE)->load(fresh, &is));
    for (int k = 0; k < 3; ++k) CHECK(std::abs(controls.physical(fresh, k) - active[k]) < 1e-3);
    fresh->destroy(fresh);
}
}

int main(int argc, char** argv) {
    REQUIRE(argc == 2);
    Module module(argv[1]);
    const clap_plugin* p = module.create();
    expectPorts(p, 2); // default layout stays stereo
    auto* configs = ext<clap_plugin_audio_ports_config>(p, CLAP_EXT_AUDIO_PORTS_CONFIG);
    REQUIRE(configs != nullptr); // RVox supports mono, so the host must be offered a mono config
    REQUIRE(configs->count(p) == 2);
    clap_audio_ports_config a{}, b{}, extra{};
    REQUIRE(configs->get(p, 0, &a) && configs->get(p, 1, &b));
    CHECK(!configs->get(p, 2, &extra));
    CHECK(a.id != b.id && a.id != CLAP_INVALID_ID && b.id != CLAP_INVALID_ID);
    const auto mono = a.main_input_channel_count == 1 ? a : b;
    const auto stereo = a.main_input_channel_count == 1 ? b : a;
    for (const auto* c : {&mono, &stereo}) {
        const uint32_t n = c == &mono ? 1u : 2u;
        const char* type = n == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
        CHECK(c->input_port_count == 1 && c->output_port_count == 1 && c->has_main_input && c->has_main_output);
        CHECK(c->main_input_channel_count == n && c->main_output_channel_count == n);
        CHECK(c->main_input_port_type && std::strcmp(c->main_input_port_type, type) == 0);
        CHECK(c->main_output_port_type && std::strcmp(c->main_output_port_type, type) == 0);
    }
    clap_audio_ports_config again{};
    CHECK(configs->get(p, 0, &again) && again.id == a.id); // stable ids
    const clap_plugin* other = module.create();
    clap_audio_ports_config otherA{};
    CHECK(configs->get(other, 0, &otherA) && otherA.id == a.id); // stable across instances
    other->destroy(other);
    CHECK(!configs->select(p, CLAP_INVALID_ID));
    CHECK(!configs->select(p, a.id + b.id + 100));
    expectPorts(p, 2);

    int64_t steady = 0;
    REQUIRE(configs->select(p, mono.id));
    expectPorts(p, 1);
    processActive(p, 1, steady);
    CHECK(!configs->select(p, stereo.id)); // active reconfiguration is refused
    expectPorts(p, 1);
    p->deactivate(p);

    REQUIRE(configs->select(p, stereo.id));
    expectPorts(p, 2);
    processActive(p, 2, steady);
    CHECK(!configs->select(p, mono.id));
    expectPorts(p, 2);
    p->deactivate(p);

    stateRoundtrip(module, p);
    p->destroy(p);
    module.entry->deinit();
    std::printf("rvox.clap_abi: %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
