# DeEsser (Tsunami De-Esser) evidence

Status: **partial target, not accepted**. The independently drawn editor and portable core are built into actual VST3/CLAP through the shared Adapter. Head `8ddaa2f` has native Windows/Linux product, mono, state, protocol and hosted-editor evidence (run `40571786472`); its numeric window, filter-shape selector and typed-entry/gesture regressions passed on both OSes. The newer finite-overload repair below is locally tested but still needs its own native build. CLAP mono advertisement remains a concrete SDK/adoption gap. Stored genuine-original comparisons cover eleven calibrated/held-out cases; no new reference capture or genuine mid-stream automation comparison is claimed.

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
- Native product wrapper: see "Native wrapper integration" and "Native product workflow and Energy/Atten bridge" at the end. Energy/Atten in the product build show the core's current post-block detector level / gain reduction (source-grounded on the core's own detector and gain computations; NOT genuine-original meter ballistics evidence). The UI timer samples only the most recent block, so the editor's peak readouts hold the maximum of the sampled values, not of every block.

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

## Native wrapper integration (first slice, 890d808; PR23 since merged at 094ec6d)
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

## Native product workflow and Energy/Atten bridge (after merging main 094ec6d)
- Core accessor: `Processor::meterCount = 2` and `meter(0|1)`. It returns the detector level / gain reduction of the most recent non-empty `process()` block, on the same scales as the held `take*()` meters (unchanged). It is observation only.
  - Audio witness: the real-Adapter render hash `AUDIO_FNV1A64 c19f9aa561842753` over all 16x512 stereo output samples is identical before (140b09f) and after the change.
  - The portable `deesser_processor`/`metadata`/`automation`/`editor_model` ctests pass.
- Editor bridge: `Editor.hpp` reads the shared `CoreMeterSource` (`coreMeter(0/1)`) when no plugin-local `DetectorMeterSource` exists.
  - RED: `adapter_test.cpp` failed to compile with `'class tsunami::deesser::Processor' has no member named 'meter'`.
  - GREEN: published values equal the core's, the Energy/Atten bars and readouts carry data, and after 2 s of silence the published values fall while the held core peak does not (current, not lifetime max).
- `.github/workflows/deesser.yml` runs on Windows/Linux, with pinned actions and tool hashes:
  - portable core ctests;
  - root build with `TSUNAMI_PLUGIN_ID=deesser`, then VST3, CLAP and `deesser.adapter`;
  - clap-validator 0.4.1;
  - pluginval 1.0.4: Linux at strictness 6 inside the editor capture; Windows at strictness 6, sha256 `c08e61ce...` of the release asset, fetched the same way as the project's Linux pin;
  - Pedalboard rate/block/state matrix (`tests/foundation/host_smoke.py`);
  - hosted editor capture at the exact 380x380 product size, with PID, module and source hashes in the receipt. `tests/deesser/linux_editor_capture.py` and `tests/deesser/windows_editor_capture.py` load the reviewed foundation/S1 capture with a single literal substitution.
- Local Linux run of the capture: pluginval SUCCESS, window 380x380 owned by the launched PID with the exact module mapped. Atten readout `0.0`, so the bridge is live; Energy reads `--` on pluginval's silent input, because -120 dB is the floor. Known cosmetic issue: the SideChain combo text is truncated ("B...").
- The release pipeline (`tools/release.py discover`) on this tree includes `deesser` (`TsDE`, `TSUNAMI_PLUGIN_ID=deesser`).
- Meter limitations (from the independent review):
  - The editor timer (30 Hz) samples only the latest published block. At 512 samples / 48 kHz that is about 94 blocks/s, so blocks between polls are not seen and the Energy/Atten peak readouts can under-report. The manual uses the Energy peak as the Threshold guide.
  - The shared Adapter does not republish the meters after `reset()`, during host bypass or when the transport stops, so the bars can freeze at the last value. Fixing that belongs to the shared Adapter owner, and a running max inside the Adapter would change the shared contract.
  - Windows pluginval has no prior run in this project until the DeEsser CI run reports.
- Not claimed: genuine meter ballistics, original UI equivalence, release packaging, product acceptance. CI results are recorded in the PR, not here.

## Native CI run 40555152920 (e846db9) and repairs
- Exact-head result, both OSes:
  - core ctests, the root build (VST3/CLAP) and `deesser.adapter` passed. Windows `AUDIO_FNV1A64 9a6f370f8b7d276f` is MSVC output; it differs from the Linux GCC hash `c19f9aa561842753`, and parity with the portable core holds per platform.
  - clap-validator 0.4.1 failed. Linux: 30 success / 11 skipped / 1 warning / 1 failed / 1 crashed. Windows: 28 / 12 / 2 / 1 / 1.
  - All later host steps (Pedalboard, pluginval, editor capture) were skipped, so this run has no hosted DeEsser evidence.
- Crash: `process-varying-sample-rates` (SIGABRT / 0xc0000409). `Processor::prepare` threw `std::invalid_argument` for rates outside 8-384 kHz into the host.
  - Fix, a host-rate policy and NOT vendor parity: outside the accepted DSP domain the core passes audio through unchanged (as Bypass), reports no gain reduction and an idle detector (-120 dB), and resumes when re-prepared at a supported rate. As with Bypass, non-finite input is passed through raw. A bad channel count still throws.
  - RED: `FAIL prepareToPlay(1000) threw` (exit 24). GREEN: real-Adapter test passes with the in-domain hash unchanged (`c19f9aa561842753`).
  - Local pinned clap-validator on the rebuilt CLAP: `process-varying-sample-rates` success.
- Remaining failure, `param-conversions` on Freq: the round trip `'5585.6484375' -> 0.49494943 -> '5585.6474609'` is inconsistent. Text conversion for non-choice parameters comes from the shared `Adapter::layout()`: default 7-decimal text, float32 normalisation under the generated log skew. `tools/generate_metadata.py` uses step 0 for every non-choice parameter, ignoring both Freq `steps: 1001` and Threshold `step: 0.1`.
  - Not fixable in plugin scope without changing the Freq parameter law, so this is a shared-owner blocker. A suggested shared fix is bounded-precision text (or honouring `step`) for continuous parameters.
- Workflow: evidence steps after a successful build now run even when a gate after the build fails, so host/UI evidence is collected. Each failed gate still fails the job.
- Compiler warning `Processor.hpp:102:53` (line number at e846db9; the read is at line 107 after e012243): array subscript 1 is outside array bounds of 'const float* [1]'` (GCC 11.4, `-O3 -DNDEBUG -Wall`, caller `test_automation.cpp:19` after `prepare(48000,1,B)`; not emitted at `-O2`). The investigation found no invalid access:
  - At runtime `in[1]` is read only when `channels_==2`.
  - ASan+UBSan at `-O3` run clean for a minimal mono repro, `test_automation`, `test_processor`, `test_editor_model` and `splice_validity`.
  - Negative control: the same one-entry array with `prepare(...,2,...)` is reported by ASan as `stack-buffer-overflow ... Processor.hpp:102` (e846db9 numbering), which is a caller contract violation.
  - Conclusion: the warning is a false positive on an inlined path the compiler cannot prove unreachable. No source change, and the warning is not suppressed.

## CLAP text round trip (param-conversions) repair
- Failure, native run 40555152920 on both OSes. clap-validator 0.4.1 `param-conversions` on Freq at grid sample 49: normalised 0.494949494949495 -> `'5585.6484375'` -> 0.49494943 -> `'5585.6474609'`. JUCE's default 7-decimal text for an interval-0 parameter exposes one-ULP float error from the log-skew conversion.
- Fix (text only), with this worker temporarily owning the shared seam:
  - `shared/Parameter.hpp` has a trailing `int displayDecimals = -1`; -1 keeps JUCE's default.
  - `tools/generate_metadata.py` accepts an optional `display_decimals`: an integer 0..6 on non-choice parameters only. Negative values, bools, fractions, strings, null and values > 6 are rejected. When set, it emits the skew and `nullptr` choices slots before it.
  - `shared/Adapter.hpp` adds a `withStringFromValueFunction` only for explicit non-choice precision: `juce::String(roundToInt(v))` for 0 (JUCE's `String(float, 0)` would use the default format) or `String(v, d)`. It respects a positive requested length. Parsing is unchanged.
  - DeEsser `parameters.json` sets `display_decimals: 0` on Freq only. Freq stays `type: float`, interval 0, with the same min/max/default/skew/physical law; non-integral Hz values remain representable; only the displayed string rounds.
  - S1 and fixture generated bytes are unchanged (sha256 pinned in `tests/foundation/test_metadata.py`).
- RED, real generated DeEsser Adapter (`tests/deesser/adapter_test.cpp`):
  - The test uses the exact validator grid, N = clamp(ceil(4000/6), 5, 100) = 100 per parameter, 600 cases, through `getText(v, 256)` -> `getValueForText` -> `getText`.
  - It fails `Freq i=49 '5585.6484375' -> 0.494949 -> '5585.6474609'` (exit 29).
  - The generator test also fails RED (no precision emitted).
- GREEN:
  - All 600 cases are stable, with the second-hop value equal.
  - Every whole-Hz text 2000..16000 is stable.
  - Text queries leave parameter state unchanged.
  - Threshold keeps the default 7-decimal text and choices keep their labels.
  - In-domain render hash is unchanged (`c19f9aa561842753`).
  - Generator tests pass 9/9.
- Local unchanged clap-validator 0.4.1 (sha256-pinned) on the rebuilt CLAP: exit 0, 32 success / 11 skipped / 1 warning. The warning is `state-invalid-random`: random bytes load without error and without a crash. It is reported as a warning, not as a pass.
- Local builds use a scratch root at 3b64a12 whose `CMakeLists.txt`/`cmake`/`shared`/`tools/generate_metadata.py`/`tests/foundation` are byte-identical to this branch's committed files, overlaid with this change's shared/generator files (verified with `cmp`). The foundation fixture `adapter_test` was not rebuilt locally; CI runs it.

## Editor readout/label fixes (948e848; local, not pushed)
- These defects were seen in the native c99d1b2 captures (Windows Threshold `0.000...`, SideChain `B...`/`Hi...`, Output `-0.0`) and in a local hosted frame captured before the first meter poll (full Energy/Output bars, Atten `--`).
- Each has a regression in the real-Adapter test, against the real editor. RED was witnessed for 36, 39, 40 and 41; 37/38 are guards that passed before the fix:
  - initial value-box text, `updateText()` after installing the text function (code 36);
  - whole SideChain labels: compact combo look-and-feel at 12 pt with a 14 px arrow zone; the box is 78 px, centred in the 90 px column, in the same row (37-39). RED: `'BandPass' needs 65.3 px, has 20`;
  - display-only `-0.0` normalisation for the Energy/Output readouts; real `-0.1` is still shown (40);
  - idle meters before the first poll (41).
- The render hash is unchanged (`c19f9aa561842753`). DSP, parameters and shared code are unchanged.
- Fresh hosted Linux capture of the rebuilt module (pluginval 1.0.4 strictness 6, Xvfb): SUCCESS, PID 476318, 380x380, module sha256 `63ca0b30...`, screenshot `b901a3ec...`. Visually inspected:
  - SideChain shows `BandPass` whole;
  - Energy reads `--` in that frame (no detector data in pluginval's state at capture), so the capture does not show Energy live;
  - the Frequency and Threshold boxes show editor text;
  - Atten and Output read `0.0`.

  Logs are under the campaign state `deesser-native/editor-readouts/`.
- Still pending: a native Windows capture at a new head (the initial-text fix is the Windows-visible one), original-UI equivalence. The SideChain filter-shape pictogram and the Frequency value window were added at 35b870a (see ui-checklist.md, adapter tests 49-56).

## Mono 1-in/1-out product layout (generator emission integrated on this branch; see the end of this section)
- Gap: the core supports `prepare(rate, 1, block)`, but the product wrapper advertised only stereo (the original has mono and stereo components). The shared Adapter's reviewed opt-in (`Spec::supportsMono`, `detail::SupportsMono`) is on main. The generator emission from `supports_mono` came from the RVox PR18 branch (`origin/plugin/rvox` df1a8e2) and is now integrated here (see the end of this section).
- `plugins/deesser/parameters.json` now declares `"supports_mono": true`. No DSP, editor or shared change; the generator change is described at the end of this section.
- RED, real generated DeEsser Adapter (`tests/deesser/adapter_test.cpp`):
  - The Adapter refuses mono 1-in/1-out (code 43); asymmetric layouts are already rejected (42).
  - With the metadata added, this branch's generator at 37f07cb still emitted no `supportsMono`, so the test still failed (43). That gate is resolved by the generator integration below.
- GREEN, first in a scratch preview (this branch's generator plus PR18's exact hunk, applied with offsets, outside the repo; S1 bytes unchanged), and since f40cc3c with this branch's own generator, which is byte-identical to that preview:
  - mono render bit-identical to the 1-channel core and finite (44);
  - active de-essing with Energy/Atten equal to the core (45);
  - mono control readback and state round trip (46);
  - stereo restored and bit-identical to the 2-channel core (47-48);
  - all earlier tests pass, and the stereo render hash is unchanged (`c19f9aa561842753`).
- Native host smoke, `tests/deesser/mono_host_smoke.py` (Pedalboard 0.9.26 from the hash-pinned `tools/host-requirements.txt` in a private CPython 3.11 venv). Run locally on Linux only:
  - Negative control: the stereo-only CI module from 764825e (Linux `.so` sha256 `28296486b503f96f59bf103acf049c4a9ff02c7805b69946814a3c11c82fbbbf`) is refused, `does not support 1-channel output`. Pedalboard does not silently adapt a stereo plugin to mono.
  - Mono-capable module: host-reported output (1, 8192), which is the mono proof together with Adapter check 43, max diff vs the 1-channel core `0.0` (checks processing only; with L = R a stereo core would give the same samples), 12.27 dB of de-essing at -40 dB, transparent at 0 dB, state restore reproducible, stereo diff `0.0`.
  - The workflow step was added after the pinned Pedalboard install. No Windows/Linux CI readback exists for it yet (the parent pushes).
- CLAP: clap-validator 0.4.1 on the mono-capable CLAP exits 0 (31 success / 11 skipped / 2 warnings: denormal timing, random state). The plugin implements none of `audio-ports-config`, `configurable-audio-ports` or `audio-ports-activation`, so CLAP hosts are **not** offered a mono configuration. CLAP mono advertisement is unresolved, not waived.

- Generator integration (this branch): PR18's minimal `supports_mono` hunk from `origin/plugin/rvox` df1a8e2 is applied to `tools/generate_metadata.py` (default false keeps generated bytes; non-bool rejected; emits `static constexpr bool supportsMono = true;` only on opt-in), together with its exact regression `test_mono_layout_is_explicit_opt_in` in `tests/foundation/test_metadata.py`. The generator's `display_decimals` support is preserved; PR18's whole generator was not taken.
  - RED: the new test failed on this branch's generator (no `supportsMono` emitted).
  - GREEN: metadata tests 10/10, including the pinned S1/fixture byte identity and the `display_decimals` negatives.
- Real Adapter with this branch's generator: an up-to-date (no-op) rebuild of the cached scratch-root build. No objects were recompiled, because every input was already identical: root `CMakeLists.txt`, `cmake/*`, `shared/*` and `tools/generate_metadata.py` are sha256-identical to this branch, and the build's `Generated.hpp` is byte-identical to one generated from this branch (sha256 `aa82aa7d...`). The modules are therefore the same binaries as the preview.
  - `deesser.adapter` prints PASS, which means every check including mono 42-48 passed (the test exits at the first failing check); stereo render hash `c19f9aa561842753`.
  - Mono host smoke passes (VST3 `.so` `a230fb77...`). The stereo-only negative control (`.so` sha256 `28296486...`, the 764825e CI module) is still refused before any receipt is written.
- The initial checkpoint above was local. Later heads `58073f2` and `8ddaa2f` were rebuilt and hosted natively on Windows/Linux: actual `(1, 8192)` VST3 output, 12.2663 dB active de-essing, and zero difference versus each platform's one-/two-channel portable core. CLAP mono advertisement remains unresolved.

## Finite-overload numeric safety (candidate-only repair)

- A real maximum-float stereo probe in SideChain monitor mode produced NaN audio at sample 1 on `8ddaa2f`. This is a replacement safety defect, not evidence about the original plugin's extreme-domain behavior.
- Regression-first repair: preserve the ordinary float stereo sum/half path, using a double average only when the float sum overflows. Saturate double intermediates to the representable float range when storing detector history, monitor output and split history/recombination; bypass remains raw and unchanged. No parameter law, coefficients or ordinary audio-domain clamp changed.
- The committed core regression covers mono/stereo, both Audio/Monitor settings, finite extremes, continued valid audio, finite meters, reset versus a fresh instance and raw bypass. Witnessed RED: `finite overload stays finite in audio and SideChain modes`; GREEN under halting undefined/float-cast-overflow sanitizers.
- A private differential built the exact previous independently authored Tsunami core (`8ddaa2f`, header SHA256 `dbb04e75986e4d09eb38da775be3b43f3837008a9cd1de2da44581ee452be6a3`) beside the repaired core. **1,728 normal-domain cases were bit-identical**, including three rates, mono/stereo, both detector/audio/monitor modes, four thresholds, three frequencies/amplitudes and continued block partitions. This is candidate-to-candidate regression evidence, not a new original-plugin comparison or a guarantee over every finite input.
- All four portable CTests pass, and the real Adapter's existing normal render remains `AUDIO_FNV1A64 c19f9aa561842753`. Exact-new-head native Windows/Linux verification remains required; `8ddaa2f` CI does not cover this later repair. No original runtime/capture or held target inputs were accessed.
