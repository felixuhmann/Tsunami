# DeEsser (Tsunami De-Esser) evidence

Status: portable DSP core functionally matched on every stored genuine render case below (stored older evidence; no new reference captures). **Partial target**: the code-drawn editor (`plugins/deesser/Editor.hpp`) and core are registered through `plugins/deesser/tsunami.cmake` and compile into real VST3/CLAP with the shared Adapter, locally on Linux against the unmerged shared PR23 root only (no Windows build, no current-head native CI product job yet, no host load/scan); parameter automation within a block is not modelled; block-boundary changes are tested on the Tsunami core only (no genuine automation oracle). Not marked verified.

## Implementation
`plugins/deesser/Processor.{h,cpp}` (C++17, namespace `tsunami::deesser`):
- Detector: mono sum of channels (stereo = mid, (L+R)/2), cascade of 4 biquads (3 high-pass sections at 0.84*Freq; 4th low-pass above Freq for BandPass, identity for HighPass), scaled by sqrt(1/2) — this is the SideChain monitor signal.
- Envelope peak follower (fast rise / slow fall poles), 1024-bin quantized gain curve with threshold bin, dual attack/release gain smoothers (min of fast/slow).
- Audio = Wideband: gain applied to the full signal. Audio = Split: first-order all-pass split around Freq; gain applied only to the upper band.
- Output clipped to [-1, 0.999999]; Bypass is bit-transparent. Freq is applied in whole Hz (grounded by genuine render, see below).
- Constants/structure were recovered by the prior worker from the private kernel analysis; this session validated them functionally against genuine renders, it did not re-derive them from disassembly.

`plugins/deesser/parameters.json` mirrors the genuine parameter enumeration: Freq 2000..16000 Hz (1001-step log taper 2000*8^n, default 5506), Threshold -80..0 dB step 0.1 (default 0), SideChain {BandPass, HighPass} (default HighPass), Audio {Split, Wideband} (default Split), Monitor {Output, SideChain} (default Output), Bypass.

## TDD witnesses (actual output)
- DSP unit tests vs the prior HEAD bypass-only source (50b4f2d): `FAIL: active output clips asymmetrically at unity threshold` (rc=1); vs current source: `PASS bypass, clipping, detector, reset, compression and mode behavior`.
- Metadata test before `parameters.json` existed: `FileNotFoundError ... plugins/deesser/parameters.json` (rc=1); after: `PASS deesser parameters.json matches genuine parameter list`.
- Whole-Hz frequency test before the fix: `FAIL: freq is quantized to whole Hz` (rc=1); after: PASS. Motivation: genuine 44.1 kHz chirp render at Freq raw 0.75 had max error 7.5e-4 with 9513.66 Hz vs 4.8e-6 with 9514 Hz.

Kernel unit tests make no fidelity claim; fidelity claims rest only on `comparison.json`.

## Full-plugin comparison
Thresholds were declared in `thresholds.json` before any comparison: PASS iff max_abs_error <= 1e-3 AND residual <= -60 dB.
Integer PCM inputs are decoded as 1/(2^(bits-1)-1), matching the oracle host's decoder (measured: a 32768/32767 gain ratio otherwise).
Result: `comparison.json` — all cases PASS (calibrated inputs at 44.1/48/96 kHz, mono and stereo, blocks 128/256/512, thresholds 0..-60 dB, both SideChain modes, both Audio modes, both Monitor modes; plus held-out speech evaluated only, never used for tuning). Worst max_abs_error ~2e-5, worst residual -85 dB.

## Reproduce
```
cmake -S plugins/deesser -B build/deesser && cmake --build build/deesser && (cd build/deesser && ctest --output-on-failure)
python3 tests/deesser/compare_oracle.py build/deesser/deesser_render <oracle-evidence-root>/deesser docs/evidence/deesser/thresholds.json docs/evidence/deesser/comparison.json
```
The comparison reads stored private oracle renders only; no vendor code or audio is committed.

## Block-boundary parameter changes (core only, no fidelity claim)
The genuine oracle (`runtime/oracle.py`, RUNNER_CONTRACT.md) fixes controls before processing: "sampled automation timeline not yet supported". No genuine automation comparison is therefore possible. Instead, `tests/deesser/test_automation.cpp` steps Threshold (0 <-> -40 dB) and Freq (4000 <-> 12000 Hz) between 512-sample blocks on a 7 kHz 0.5-amplitude tone at 48 kHz, in Wideband and Split, against thresholds predeclared in `thresholds.json` (`block_boundary_automation`: max per-sample gain step <= 0.05, grounded on the recovered 0.5 ms fast attack smoother, 1-exp(-1/24)=0.0408; click excess over input slew <= 0.03).
TDD witness: RED = test failed to compile (`'class tsunami::deesser::Processor' has no member named 'gainReductionDb'`); GREEN = all 8 cases PASS, worst max_gain_step 0.0341 (threshold 0->-40 Wideband), click_excess 0 everywhere (`automation-block-boundary.txt`).
Threshold changes are smoothed by the recovered attack/release gain smoothers; Freq changes recompute coefficients immediately with filter state retained (the core adds no parameter ramp, since no recovered evidence shows one).
Meters: `Processor::gainReductionDb()` (largest attenuation, positive dB) and `detectorLevelDb()` (peak detector envelope, Threshold scale) are lock-free relaxed atomics folded in with a CAS max on every `process()` and held until `take*()`/`reset()` (see round 2 task 4); audio output is unchanged (stored genuine comparison re-run: all PASS).

## Remaining limitations
- Parameter changes mid-stream not compared to the genuine plugin (oracle has no automation path); intra-block (sample-accurate) changes not modelled.
- Sample rates outside 44.1/48/96 kHz, and Freq near 16 kHz at 44.1 kHz (Nyquist clamp path), not compared.
- Native product wrapper: see "Native wrapper integration" at the end. Energy/Atten have no data in the product build (drawn empty, readouts `--`): the core's meters are max-since-`take*()`, while the shared Adapter's `meterCount`/`meter()` hook (PR23) publishes a const peek after every block, which would hold the maximum until the next `reset()`. Hooking them needs either a take-on-publish meter contract in the shared Adapter or a core accessor change; neither is done here.

## Round 2: editor and mid-stream automation
- `tests/deesser/test_editor_model.cpp` (ctest `deesser_editor_model`, no JUCE): every control maps to a genuine parameter, and no control is invented. Covers clamp/quantise, the Freq taper, display text, all-or-nothing state restore, Energy/Atten fed from the core's realtime-safe meters, and infinite peak hold with click reset.
- `tests/deesser/editor_test.cpp` (native JUCE, harness processor): builds the real `Editor.hpp` and checks:
  - UI to APVTS updates and APVTS to UI updates
  - state save/restore
  - Atten/Energy readings taken from the core
  - Output readings taken from MeterSource

  It was compiled against the pinned JUCE9 build cache, using origin/main `shared/` and the adapter_test link line, and run under xvfb. It is still a harness test; the real-Adapter product test is `tests/deesser/adapter_test.cpp` (below).
- `automation-midstream.txt`: the genuine oracle cannot apply mid-render parameter changes. The splice model was measured against the predeclared criteria in `thresholds.json` and is invalid for Threshold, Freq, SideChain (both directions) and Audio Wideband->Split; valid on the core only for Monitor and Audio Split->Wideband. No genuine mid-stream claim is made.
- `ui-checklist.md`: a control-by-control comparison against the manual figure.

## Round 2 task 4: review fixes (deesser-review-0480502)
- Core is header-only (`plugins/deesser/Processor.hpp`, INTERFACE library) so it fits `tsunami_plugin`, which compiles only `shared/Plugin.cpp` and includes `Processor.hpp`.
- Energy meter is on the Threshold scale: `detectorLevelDb()` reports the envelope / sqrt(0.5), which is exactly what the detector compares with 10^(threshold/20). Attenuation therefore starts when the slider goes below the bar's peak (manual ch.2). Before the fix it was about 3.01 dB off. Tested in `deesser_editor_model` 6a.
- Energy/Atten peak hold: the core folds every block into its meter atomics with a lock-free CAS max, and `take*()` exchanges them back to idle. `MeterModel::poll(Processor&)` takes, so blocks between UI polls are not lost (test 6b). The JUCE harness `DetectorMeterSource` path still peeks; the product path needs the foundation meter tap.
- Freq slider: the ineffective JUCE skew call was removed. A slider-side custom `NormalisableRange` (2000*8^n, whole Hz, `ui::proportionToFreq`/`freqToProportion`, test 6c) is set after the attachment. The JUCE editor test was NOT re-run in this round.
- Atten readout shows "0.0" when idle instead of "-0.0" (`ui::attenReadout`, test 6d).
- `thresholds.json` `ordering_note`: values unchanged; the self-reported declaration stamps are not provable from git.

## Native wrapper integration (Linux local build only; PR23 dependency)
- `plugins/deesser/tsunami.cmake`: plugin-local registration `tsunami_plugin(TsunamiDeEsser "${TSUNAMI_PLUGIN_DIR}" deesser "Tsunami De-Esser" TsDE "tsunami::deesser::Processor")` plus ctest `deesser.adapter`. `TsDE` is not used by any other registration (TsFx, TsS1, TsRV). It takes effect only with the shared root from PR23 (`integration/shared-foundation` @ 3b64a12): `include("${TSUNAMI_PLUGIN_DIR}/tsunami.cmake")` and generic `TSUNAMI_PLUGIN_ID`. On main (7781f52), `tools/release.py` does not discover it yet; it discovers only s1.
- The generator picks up `Editor.hpp` and emits `PluginSpec::createEditor` -> `tsunami::createPluginEditor`, so hosts get this editor, not the fixture fallback. Parameter ranges, choice labels and the log taper come from PR23's metadata generator; DSP and parameter mapping are unchanged in this slice.
- `tests/deesser/adapter_test.cpp` (real `Adapter<PluginSpec>` via `createPluginFilter()`) checks:
  - identity and the 6 parameters (order, range, default, choice labels)
  - that this editor is attached, with UI->parameter and host->UI bindings
  - state round trip into parameters and UI
  - a stereo 7 kHz render bit-identical to `tsunami::deesser::Processor` and attenuated at Threshold -40 dB
  - Output meters fed by `MeterSource`
  - Energy/Atten not fed (`--`, bars empty) and no `CoreMeterSource`/`DetectorMeterSource` exposed
  - the painted editor snapshot is 380x380
- Witness (logs in campaign state `state/deesser-native/`):
  - RED: configuring the PR23 root with `TSUNAMI_PLUGIN_ID=deesser` failed with `plugins/deesser has no tsunami.cmake registration`.
  - GREEN: configure OK, then `TsunamiDeEsser_VST3`, `TsunamiDeEsser_CLAP` and `deesser_adapter_test` built with pinned JUCE 3fb5cbc and the cached clap-juce-extensions.
  - Exports: `GetPluginFactory`/`ModuleEntry`/`ModuleExit` (VST3 .so) and `clap_entry` (.clap).
  - Test result: `xvfb-run ctest -R deesser.adapter` Passed (`PASS DeEsser real Adapter: identity, editor bindings, state, render parity, meters`).
- Reproduce: `cmake -S <PR23 root> -B build -DTSUNAMI_BUILD_FIXTURE=OFF -DTSUNAMI_PLUGIN_SOURCE_ROOT=<this repo> -DTSUNAMI_PLUGIN_ID=deesser -DTSUNAMI_JUCE_SOURCE=<pinned JUCE> -DTSUNAMI_CLAP_SOURCE=<clap-juce-extensions>`, then `cmake --build build --target deesser_adapter_test TsunamiDeEsser_VST3 TsunamiDeEsser_CLAP`, then `xvfb-run -a ctest --test-dir build -R deesser.adapter`.
- Not claimed:
  - Windows build
  - current-head native CI product job
  - host scan/load (pluginval/clap-validator)
  - release packaging
  - acceptance

  Generic S1/foundation CI results are not DeEsser evidence. Sidechain input and mono buses are not exposed: the wrapper is stereo in/out only, and the internal SideChain filter and Monitor modes are the existing parameters.
