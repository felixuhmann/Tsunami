# S1 Imager Stereo — tested partial DSP milestone

Independent portable C++17 implementation for **S1 Imager Stereo**, not a completed product or full-equivalence claim. Separate Shuffler and MS-Matrix components are excluded. No vendor code, audio, binaries, recovered pseudocode or graphics are included.

## Preserved checkpoint and TDD provenance

The interrupted worktree contained uncommitted matrix/control/smoothing implementation, processor assertions, renderer and comparison script. These were reviewed and preserved in `9c9b386`, then latest main (`68b2a1e`) was integrated without discarding them. Their original RED-before-GREEN history is unavailable: **no strict TDD provenance is claimed for inherited DSP**. Newly added adapter metadata has a witnessed RED (`S1 core must provide adapter metadata`) followed by GREEN. Additional inherited-DSP safety assertions are regression coverage, not retroactive TDD claims.

The core implements width-dependent normalization, gain, rotation, asymmetry, input-column polarity/swap, M/S input decode and 40-ms coefficient smoothing. Physical gain is -24..0 dB. Unsupported channel layouts are safety passthrough, not reference variants. Replacement bypass is explicit; it is not claimed to reproduce vendor host-bypass transitions.

## Fresh genuine comparisons

See [comparison-summary.json](comparison-summary.json) for capture IDs, exact reference/version/file hashes, input/output hashes, candidate source/renderer hashes, control readback, scope and thresholds. All original captures used the mandatory oracle lock, snapshot-hashed host script and isolated nonroot/no-network containers. Environment: Windows original 17.1.42.50 under Wine 9 / Pedalboard 0.9.26 versus native Linux portable candidate; **not native Windows candidate verification**.

Seven fresh captures: six calibration cases and one reserved held-out utterance, all at 48000 Hz / block 512, fresh instance/reset, fixed controls. Maximum-absolute error gate `< 2e-6` existed before the fresh renders. RMS reporting gate `< 2e-7` was added after first measurements; no DSP coefficients were changed or tuned on these measurements or held-out results.

| Case | Split | Stereo frames | Maximum absolute error | RMS error |
|---|---|---:|---:|---:|
| unity | calibration | 8192 | 1.49011612e-08 | 2.37472179e-09 |
| width3 | calibration | 8192 | 1.19209290e-07 | 1.60819955e-08 |
| rotation | calibration | 8192 | 2.98023224e-08 | 2.63534304e-09 |
| combined | calibration | 8192 | 1.49011612e-08 | 4.13503135e-09 |
| phase_swap | calibration | 8192 | 1.49011612e-08 | 2.37472179e-09 |
| input_ms | calibration | 8192 | 2.98023224e-08 | 3.73970066e-09 |
| heldout | reserved evaluation | 144000 | 1.49011612e-08 | 9.76623586e-10 |

A negative control intentionally ran the unity candidate against genuine width3 output: comparison rejected it (exit 1), max error `0.3589386493`, RMS `0.1485659202`. The gate therefore distinguishes this implementation from the old identity-only checkpoint.

The held-out material was reserved before evaluation: first three seconds of `6930-75918-0007-snr10-48000.wav`, stereo construction L=mono/R=137-sample delayed mono, PCM16 input. Source attribution: LibriSpeech ASR corpus, Vassil Panayotov, Guoguo Chen, Daniel Povey and Sanjeev Khudanpur; [OpenSLR SLR12](https://www.openslr.org/12), CC BY 4.0. Private source manifest and hashes are retained; no speech/reference audio is published.

PCM16 decoding in the comparator follows the observed Pedalboard AudioFile conversion `/32767`; the independent unity capture calibrates this input interpretation (max error `1.49e-8`). It does not cover other file encodings. Shape, finite samples and reference sample hash are checked before measurement.

## Real local execution

```
cmake -S plugins/s1 -B /absolute/private/build
cmake --build /absolute/private/build -j2
ctest --test-dir /absolute/private/build --output-on-failure
```

Result: **3/3 CTest cases passed** (`s1_processor`, `s1_safety`, `s1_metadata`). Shared foundation Python tests: **11 passed**. Safety executable output:

```
safety: 16 rate/block cases; max partition error=0; inplace exact; finite; silence exact; controls bounded; process/control allocations=0
```

The 16 cases combine 44100/48000/96000/192000 Hz with blocks 1/64/512/4096 and exercise an active coefficient transition. Allocation instrumentation counts C++ `new`/`new[]`, not every possible libc allocation; this is not a hardware realtime certification. Silent audio produces exactly zero, in-place matches out-of-place, invalid parameters are ignored/clamped, and latency/tail report zero.

Private capture reproduction, after provisioning the original:

```
python3 /private/runtime/oracle.py render --target s1 --component 'S1 Imager Stereo' --input /private/probe.wav --rate 48000 --block 512 --params '{"width":{"value":"3.00 Deg"}}'
python3 tests/s1/compare_oracle.py /absolute/private/build/s1_render /private/capture 0 3
```

## Actual native Linux target build and host smoke

The real `TsunamiStereo_VST3` and `TsunamiStereo_CLAP` targets built successfully using pinned JUCE 9.0.4 and the existing shared adapter, in a private isolated build with two compiler jobs under heavy-slot 1. No root/shared source was edited. The shared generic editor emits a pre-existing deprecated-font-constructor warning; this is not a product-UI implementation.

[Native Linux receipt](native-linux-summary.json) records exact module/core/metadata/test hashes. The actual VST3 loaded in native Linux Pedalboard 0.9.26 and passed **20 rate/block cases** (44100/48000/96000/192000 Hz × 1/17/64/257/1024). All nine controls restored from state; a fresh restored instance produced exactly the same output. At combined physical controls `[-6, 2, -22.5, -30, 0, 0, 0, 0, 0]`, hosted output matched the independently tested portable renderer exactly (worst error **0**). Replacement bypass and silent output were exact; reported latency was **0 samples**. This tests the adapter path, not additional original-plugin equivalence at those rates.

Reproduce with the pinned host environment and a real native module:

```
python tests/s1/native_host_smoke.py '/path/Tsunami Stereo.vst3' /path/s1_render /private/native-host-receipt.json
```

These are local experimental binaries, not publicly packaged artifacts or native Windows evidence. The source hashes identify the compiled worktree contents; test/docs were committed after the build. Native CI still exercises the infrastructure fixture only.

Mature native validators also executed on these exact modules: **pluginval 1.0.4 strictness 5: SUCCESS**, including Editor/Editor Automation on the existing generic editor (not product-layout acceptance); **CLAP validator 0.4.1: exit 0, 31 success, 11 skipped, 2 warnings**. Skips concern unimplemented optional preset discovery, float64, note or configurable-port features. Warnings remain explicit: denormal-input processing measured 3.71× slower, and random state bytes were accepted without crashing. These are follow-up issues, not an all-tests-green or realtime-certification claim. Validator/tool/log hashes and every non-success case appear in the native receipt.

## Still outstanding — keep draft

- Genuine reference automation/continuing-state and broader rate/block/edge-control comparisons. Current numerical result establishes only the listed static scope. Short captures do not characterize demo interruptions; original outputs are preserved without mute repair.
- Independently drawn original-layout working product UI and genuine hosted side-by-side screenshots. The shared adapter currently hardcodes the generic fixture editor and exposes no plugin-local factory/APVTS hook; a scoped parent integration request is recorded outside git. No generic panel screenshot is presented as a product UI.
- Reference GUI capture attempt in the existing sandbox hit Windows CPython `init_sys_streams: OSError [WinError 6] Invalid handle`; no screenshot was produced or fabricated.
- Native Windows/Linux **target** plugin host/CI validation, artifacts, full state/control coverage, packaging and independent exact-head parent review. Existing foundation CI tests an infrastructure fixture, not S1.

Private acquisition/decompilation/raw reference evidence remains outside this repository under the campaign's private reference tree. Public evidence contains aggregate measurements and identifiers/hashes only. Worker status remains unverified.
