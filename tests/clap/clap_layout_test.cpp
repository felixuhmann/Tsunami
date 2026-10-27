// SPDX-License-Identifier: GPL-3.0-or-later
// Thin CLAP host harness: loads a REAL compiled .clap module and drives clap.audio-ports-config,
// clap.audio-ports, params, state and process through the pinned CLAP ABI. No mocked plugin outputs.
// usage: clap_layout_test <module.clap> <plugin-id> mono-stereo|stereo-only
#include <clap/clap.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
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
const void* hostExtension(const clap_host*, const char*) { return nullptr; }
void hostRequest(const clap_host*) {}
const clap_host host{CLAP_VERSION_INIT, nullptr, "Tsunami layout harness", "Tsunami", "", "0",
                     hostExtension, hostRequest, hostRequest, hostRequest};

uint32_t noEvents(const clap_input_events*) { return 0; }
const clap_event_header* noEvent(const clap_input_events*, uint32_t) { return nullptr; }
bool dropEvent(const clap_output_events*, const clap_event_header*) { return true; }
struct OneEvent { clap_input_events list; clap_event_param_value event; };
uint32_t oneSize(const clap_input_events*) { return 1; }
const clap_event_header* oneGet(const clap_input_events* l, uint32_t i) {
    return i == 0 ? &static_cast<const OneEvent*>(l->ctx)->event.header : nullptr;
}

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
    const clap_plugin* create(const char* id) const {
        auto* factory = static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        REQUIRE(factory != nullptr);
        const clap_plugin* plugin = factory->create_plugin(factory, &host, id);
        REQUIRE(plugin != nullptr && plugin->init(plugin));
        return plugin;
    }
};

template<class T> const T* ext(const clap_plugin* p, const char* id) { return static_cast<const T*>(p->get_extension(p, id)); }

struct PortInfo { uint32_t inputs, outputs; clap_audio_port_info in, out; };
PortInfo readPorts(const clap_plugin* p) {
    auto* ports = ext<clap_plugin_audio_ports>(p, CLAP_EXT_AUDIO_PORTS);
    REQUIRE(ports != nullptr);
    PortInfo r{ports->count(p, true), ports->count(p, false), {}, {}};
    REQUIRE(r.inputs == 1 && r.outputs == 1);
    REQUIRE(ports->get(p, 0, true, &r.in) && ports->get(p, 0, false, &r.out));
    return r;
}
void expectPorts(const clap_plugin* p, uint32_t channels) {
    const auto r = readPorts(p);
    const char* type = channels == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    CHECK(r.in.channel_count == channels && r.out.channel_count == channels);
    CHECK(r.in.port_type && std::strcmp(r.in.port_type, type) == 0);
    CHECK(r.out.port_type && std::strcmp(r.out.port_type, type) == 0);
    CHECK(r.in.in_place_pair == r.out.id && r.out.in_place_pair == r.in.id && r.in.id != r.out.id);
    CHECK((r.in.flags & CLAP_AUDIO_PORT_IS_MAIN) && (r.out.flags & CLAP_AUDIO_PORT_IS_MAIN));
}

clap_id gainId(const clap_plugin* p, double& lo, double& hi) {
    auto* params = ext<clap_plugin_params>(p, CLAP_EXT_PARAMS);
    REQUIRE(params != nullptr && params->count(p) == 1);
    clap_param_info info{};
    REQUIRE(params->get_info(p, 0, &info));
    lo = info.min_value; hi = info.max_value;
    return info.id;
}
double paramValue(const clap_plugin* p, clap_id id) {
    double v = -1;
    REQUIRE(ext<clap_plugin_params>(p, CLAP_EXT_PARAMS)->get_value(p, id, &v));
    return v;
}

// Processes one block of a known channel-distinct signal while automating fixture gain to 0.5 at
// sample 0. Buffers carry sentinels on both sides; the plugin must touch only frames [0, n).
void processKnown(const clap_plugin* p, uint32_t channels, uint32_t frames, int64_t steady) {
    constexpr uint32_t guard = 16;
    constexpr float sentinel = 1234.5f;
    std::vector<std::vector<float>> in(channels, std::vector<float>(frames + 2 * guard, sentinel));
    std::vector<std::vector<float>> out(channels, std::vector<float>(frames + 2 * guard, sentinel));
    std::vector<float*> inPtr, outPtr;
    for (uint32_t c = 0; c < channels; ++c) {
        for (uint32_t n = 0; n < frames; ++n)
            in[c][guard + n] = c == 0 ? 0.5f * float(std::sin(0.0625 * double(n + 1)))
                                      : -0.3f * float(std::cos(0.23 * double(n))) + 0.1f;
        inPtr.push_back(in[c].data() + guard);
        outPtr.push_back(out[c].data() + guard);
    }
    double lo = 0, hi = 0;
    const clap_id id = gainId(p, lo, hi);
    const double target = lo + (0.5 - 0.0) / (2.0 - 0.0) * (hi - lo); // fixture gain range is [0, 2], linear
    OneEvent events{};
    events.list = {&events, oneSize, oneGet};
    events.event.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    events.event.param_id = id;
    events.event.note_id = -1; events.event.port_index = -1; events.event.channel = -1; events.event.key = -1;
    events.event.value = target;
    clap_output_events outEvents{nullptr, dropEvent};
    clap_audio_buffer inBuf{inPtr.data(), nullptr, channels, 0, 0}, outBuf{outPtr.data(), nullptr, channels, 0, 0};
    clap_process proc{steady, frames, nullptr, &inBuf, &outBuf, 1, 1, &events.list, &outEvents};
    CHECK(p->process(p, &proc) != CLAP_PROCESS_ERROR);
    double energy = 0;
    for (uint32_t c = 0; c < channels; ++c) {
        for (uint32_t g = 0; g < guard; ++g) {
            CHECK(in[c][g] == sentinel && in[c][guard + frames + g] == sentinel);
            CHECK(out[c][g] == sentinel && out[c][guard + frames + g] == sentinel);
        }
        bool exact = true;
        for (uint32_t n = 0; n < frames; ++n) {
            const float y = out[c][guard + n];
            CHECK(std::isfinite(y));
            exact = exact && std::abs(y - 0.5f * in[c][guard + n]) <= 1e-6f;
            energy += double(y) * double(y);
        }
        if (!exact) std::fprintf(stderr, "channel %u of %u does not equal 0.5 * its own input\n", c, channels);
        CHECK(exact);
    }
    CHECK(energy > 1.0);
    if (channels == 2) {
        bool distinct = false;
        for (uint32_t n = 0; n < frames; ++n) distinct = distinct || out[0][guard + n] != out[1][guard + n];
        CHECK(distinct);
    }
    CHECK(std::abs(paramValue(p, id) - target) < 1e-6);
}

void run(const clap_plugin* p, uint32_t channels, int64_t& steady) {
    REQUIRE(p->activate(p, 48000.0, 1, 512));
    REQUIRE(p->start_processing(p));
    p->reset(p);
    processKnown(p, channels, 256, steady); steady += 256;
    processKnown(p, channels, 512, steady); steady += 512;
    p->stop_processing(p);
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
void stateRoundtrip(const Module& module, const char* pluginId, const clap_plugin* p) {
    double lo = 0, hi = 0;
    const clap_id id = gainId(p, lo, hi);
    const double saved = paramValue(p, id);
    CHECK(std::abs(saved - (lo + 0.25 * (hi - lo))) < 1e-6);
    Stream stream;
    clap_ostream os{&stream, writeStream};
    REQUIRE(ext<clap_plugin_state>(p, CLAP_EXT_STATE)->save(p, &os) && !stream.bytes.empty());
    const clap_plugin* fresh = module.create(pluginId);
    CHECK(std::abs(paramValue(fresh, id) - saved) > 1e-3); // fresh default differs from the automated value
    clap_istream is{&stream, readStream};
    REQUIRE(ext<clap_plugin_state>(fresh, CLAP_EXT_STATE)->load(fresh, &is));
    CHECK(std::abs(paramValue(fresh, id) - saved) < 1e-6);
    fresh->destroy(fresh);
}
}

int main(int argc, char** argv) {
    REQUIRE(argc == 4);
    const std::string mode = argv[3];
    REQUIRE(mode == "mono-stereo" || mode == "stereo-only");
    Module module(argv[1]);
    const clap_plugin* p = module.create(argv[2]);
    auto* configs = ext<clap_plugin_audio_ports_config>(p, CLAP_EXT_AUDIO_PORTS_CONFIG);
    int64_t steady = 0;
    expectPorts(p, 2); // default layout stays stereo for every processor
    if (mode == "stereo-only") {
        // Ordinary processors keep upstream behaviour: no selectable layouts, so no mono offer.
        CHECK(configs == nullptr || configs->count(p) == 0);
        run(p, 2, steady);
        p->deactivate(p);
        expectPorts(p, 2);
    } else {
        REQUIRE(configs != nullptr);
        REQUIRE(configs->count(p) == 2);
        clap_audio_ports_config a{}, b{}, extra{};
        REQUIRE(configs->get(p, 0, &a) && configs->get(p, 1, &b));
        CHECK(!configs->get(p, 2, &extra));
        CHECK(a.id != b.id && a.id != CLAP_INVALID_ID && b.id != CLAP_INVALID_ID);
        const auto& mono = a.main_input_channel_count == 1 ? a : b;
        const auto& stereo = a.main_input_channel_count == 1 ? b : a;
        for (const auto* c : {&mono, &stereo}) {
            const uint32_t n = c == &mono ? 1u : 2u;
            const char* type = n == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
            CHECK(c->input_port_count == 1 && c->output_port_count == 1);
            CHECK(c->has_main_input && c->has_main_output);
            CHECK(c->main_input_channel_count == n && c->main_output_channel_count == n);
            CHECK(c->main_input_port_type && std::strcmp(c->main_input_port_type, type) == 0);
            CHECK(c->main_output_port_type && std::strcmp(c->main_output_port_type, type) == 0);
            CHECK(std::strlen(c->name) > 0);
        }
        // IDs are stable across queries and instances.
        clap_audio_ports_config again{};
        CHECK(configs->get(p, 0, &again) && again.id == a.id);
        CHECK(!configs->select(p, CLAP_INVALID_ID));
        CHECK(!configs->select(p, a.id + b.id + 100));
        expectPorts(p, 2);

        REQUIRE(configs->select(p, mono.id));
        expectPorts(p, 1);
        run(p, 1, steady); // active: reconfiguration must be rejected and leave mono in place
        CHECK(!configs->select(p, stereo.id));
        expectPorts(p, 1);
        p->deactivate(p);

        REQUIRE(configs->select(p, stereo.id));
        expectPorts(p, 2);
        run(p, 2, steady);
        CHECK(!configs->select(p, mono.id));
        p->deactivate(p);

        REQUIRE(configs->select(p, mono.id)); // reselect and re-activate
        expectPorts(p, 1);
        run(p, 1, steady);
        p->deactivate(p);
        CHECK(configs->select(p, stereo.id));
        expectPorts(p, 2);
    }
    stateRoundtrip(module, argv[2], p);
    p->destroy(p);
    module.entry->deinit();
    std::printf("%s: %s (%d failures)\n", argv[2], failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
