# Shared native foundation (bounded milestone)

This branch owns build/CI/shared adapter/tooling only. **No target is verified.**
The explicitly named `Tsunami Infrastructure Fixture` lives under
`tests/foundation/fixture`; it tests plumbing and is not substitute matched DSP.

## Selected stack and scope

- JUCE **9.0.4**, `3fb5cbc0340758596dac39f7c8de47d562b4896e`,
  selecting its AGPLv3 open-source alternative, explicitly authorized by the user.
  Archive SHA256: `2bb7095497a7bb7269dbdf841a7c6aa0939eaeb2a2bc9724ac4758d3ae3565a7`.
- CLAP JUCE extension `7adee3a1bd4684d4caa5601100e364abccff4b4d`,
  still upstream main HEAD at migration audit (2026-10-27), newer than its
  0.26.0 tag. Retained because it is already the newest upstream revision;
  its README claims only JUCE 6/7/8, so JUCE9 compatibility requires actual
  build and validator evidence, not assumed vendor support. Pinned
  CLAP/CLAP-helpers submodules: see `dependencies.lock.json`.
- No generic DSP algorithms or unnecessary primitive dependencies.
- Stereo float32 adapter for the portable core contract. Physical parameter
  ranges/defaults, stable string IDs/version hints, block-boundary atomics,
  core latency/tail forwarding, reset, versioned plugin-bound state, original
  simple metadata-driven JUCE editor with working slider attachments.
- State serialization and GUI are non-realtime only. Core set/process calls
  are serialized on the host audio/lifecycle thread. No sample-offset
  automation promise. Mono, sidechains, transport and logarithmic/discrete
  display strings need explicit follow-up, not inferred support.

## Build and tests

Linux requires CMake >=3.22, C++17, X11 family/FreeType/fontconfig headers, ALSA for
standalone. No WebKit/curl/OpenGL runtime is requested. Windows uses native
MSVC x64 on `windows-2022`; Linux uses `ubuntu-22.04`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target adapter_test TsunamiFixture_VST3 TsunamiFixture_CLAP --parallel 2
# Linux:
xvfb-run -a ctest --test-dir build -C Release --output-on-failure
# Windows:
ctest --test-dir build -C Release --output-on-failure
python -m unittest discover -s tests/foundation -v
```

Local pinned dependency overrides: `TSUNAMI_JUCE_SOURCE`,
`TSUNAMI_CLAP_SOURCE`. For an independently written real S1 core snapshot,
set `-DTSUNAMI_PLUGIN_SOURCE_ROOT=/absolute/private/snapshot
-DTSUNAMI_PLUGIN_ID=s1` and build `TsunamiStereo_VST3` / `TsunamiStereo_CLAP`.
The external root must contain `plugins/s1/Processor.hpp` and `parameters.json`.
No worker files are copied into this PR. Missing/unknown targets fail configure
explicitly rather than building dummy DSP.

Metadata schema: object with `parameters` array; contiguous `index`, unique
lowercase `id`, `name`, `min`, `max`, `default`, optional `unit` and `type`.
`bool`/`boolean`/`int`/`integer`/`enum` use unit physical steps. All others are
continuous. Only linear normalized-to-physical mapping is implemented here.

## Native host evidence

`tests/foundation/host_smoke.py` uses existing Pedalboard **0.9.26** (not a
custom host), valid self-generated state, fresh instance, nonzero seeded
stereo renders at 44.1/48/96 kHz and blocks 1/17/64/257/1024. Hash-locked
CPython 3.11 host wheels are separate tooling. Pedalboard compensates declared
latency: this does not measure raw device latency or certify reference fidelity.

Its optional capture calls actual `show_editor()` on the main thread and
captures the full native desktop. Nonblank pixels are only an error guard;
**human review of attached plugin controls is required**, and this is not CLAP
UI proof. Hosted Windows GUI capture remains unknown until actual successful
output and review. Wine is not native Windows evidence.

CI retains matching project/dependency source archives beside experimental
binaries, LICENSE/notices, test logs, host receipt and actual screenshots when
available. Artifacts are **not releases**. Full embedded third-party SBOM,
package/download/rebuild smoke, protocol validators on both native platforms,
DAW interactions, hardware realtime behavior, and all per-target acceptance
remain blockers. Do not merge, publish or mark any target complete from this
foundation's fixture checks.

## TDD evidence

Private campaign logs are under
`/home/felix/work/tsunami-campaign/foundation-logs/`: metadata descriptor
RED→GREEN, invalid ranges/indices RED→GREEN, discrete step RED→GREEN;
compiled adapter tracer RED reports the missing shared adapter before its
implementation. Final native CTest/build/validator results are recorded in the
campaign state and PR body, not invented here.
