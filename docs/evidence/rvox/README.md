# RVox — portable core, hosted VST3/CLAP adapter and product editor (draft, not product-complete)

This is an independently written C++17 mono/stereo processor, not a vendor-library
wrapper and not a generic ratio/knee compressor. No reference binaries, assets,
audio, recovered pseudocode or preset chunks are included.

## Implemented signal path

- Three parallel first-order allpass detector branches per channel (`0.6303`,
  `0.3583`, `0.1607`), taking the maximum absolute branch/input sample. Stereo
  shares one maximum detector and gain while retaining separate audio delays.
- Rational compression gain `t / (peak * (1 - t) + t)`, with
  `t = 10^(compression_dB / 20)` and inverse-threshold makeup.
- Power-of-two approximately 1ms lookahead, sliding minimum gain, 250/500ms
  envelope poles and the recovered level-dependent, 256-entry release tables.
- Instant-attack gate envelope with **130ms** release, inverse-dB gate scaling,
  and the recovered `<= -79dB` disabled coefficient path. `-80` is the finite
  adapter sentinel for the original displayed minus-infinity setting.
- Quantized triangular gain smoothing using integer second differences;
  independently evaluated direct FIR regression; delayed mono/stereo outputs.
- Physical Compression/Gate/Gain controls, prepare/reset, in-place processing,
  zero-length calls, declared lookahead latency and finite audio-delay tail.

The 130ms value was recovered from a fresh, unmodified Windows original's saved
RealWorld state, then connected to the previously recovered Windows coefficient
builder's milliseconds-to-pole equation. It is a hidden initialization value,
not an exposed replacement control. This fixes the interrupted checkpoint's
incorrect 100ms initialization; it was not fitted to the comparison waveform.

## Provenance and TDD

The interrupted dirty `Processor.h`, existing tests and untracked renderer were
reviewed, privately backed up and preserved in commit `4e52e42`. Their original
implementation/test order was **not witnessed and is not claimed as TDD**.
Current `origin/main` was subsequently merged without discarding that work.

Changes in this checkpoint have witnessed red/green evidence:

- The direct-FIR gate test failed with max absolute error `0.000732729793526`
  at 100ms, then passed with **zero difference** after changing initialization
  to the original's 130ms value.
- Renderer tests first demonstrated silent acceptance of a partial stereo frame
  and a NaN control; both now reject before producing an output file.
- Shared metadata inclusion failed for the missing `Processor.hpp` entrypoint;
  the forwarding header now compiles with the existing metadata generator.

Rate/channel, reset, block partition, in-place and linked-stereo tests
characterize the preserved chain. They are not retroactive TDD evidence.

## Actual original comparisons

Reference: Renaissance Vox **17.1.42.50**, components **RVox Mono / RVox Stereo**.
Original execution used unmodified Windows CPython/Pedalboard under Wine, in the
existing no-network/nonroot/resource-limited container. Both a heavy-job slot
and the common `oracle.lock` were held. Host/runner/input/plugin hashes and exact
control readbacks remain in private capture receipts. This is **Wine original
execution**, not native Windows replacement evidence.

Original RVox.dll SHA256:
`56bd5c6b986ce9a1881385fd63bd3fde4f2c9c731c5762402d02ff68cea16ed8`.
Original kernel-container DLL SHA256:
`f2c60ba6f90feaa75adb8bdc4a6437572e0d091687bac8b09782646856c69822`.

All listed fresh captures explicitly read back Bypass=Off, Compression/Gate/Gain.
The gate regression settings are `-18/-20/-3dB`; the deeper case is
`-30/-40/-9dB`. PCM16 input decoding follows the previously calibrated host
`sample/32767` convention; held-out speech input is float32 WAV. Candidate
renderer latency compensation is explicit, not an optimized cross-correlation.

| Fresh capture | Rate / channels / original block | Max abs error | RMS error |
|---|---|---:|---:|
| changing noise/sine, gate | 48000 / 1 / 127 | 7.688999e-6 | 1.229744e-7 |
| changing noise/sine, gate | 48000 / 2 / 257 | 3.129244e-6 | 1.830469e-8 |
| deeper controls | 48000 / 1 / 512 | 4.440546e-6 | 5.728869e-8 |
| changing-level tones | 96000 / 1 / 127 | 3.278255e-7 | 2.610745e-8 |
| changing-level tones | 44100 / 2 / 127 | 1.788139e-7 | 2.341096e-8 |
| reserved speech+noise, 8 seconds | 48000 / 1 / 127 | 1.192093e-7 | 5.783533e-9 |

The held-out input is the existing LibriSpeech-derived `6930-75918-0007`, 10dB
SNR fixture. It was evaluated only after the gate fix; no tuning used it.
Its waveform and original output are not distributed here.

The interrupted mono gate maximum mismatch was `0.00724128075`, stereo
`0.00726862624`; fixing initialization materially improves both. Nevertheless,
a tight `max_abs <= 1e-6` / `RMS <= 1e-8` diagnostic **still fails** on the 48k
mono changing-level probe. Residual transient/rounding differences are unresolved.
An examined recovery-index rounding rearrangement did not improve those captures
and was reverted rather than presented as a fix.

Reporting guards `max_abs <= 1e-5`, `RMS <= 2e-7` were chosen **after** these
captures for regression reporting; they are not predeclared full-equivalence
acceptance thresholds. All six captures meet those guards. A deliberately wrong
compression setting (0 instead of -18dB) on the held-out input is rejected:
max error `0.3337684423`, RMS `0.0444648774`. The comparator therefore does not
accept a control-insensitive/unity replacement.

## Makeup precision and numeric-domain policy (after review of 5389ab8)

- **Makeup precision:** the recovered coefficient builder keeps the
  compression threshold in double for the inverse-threshold makeup and converts
  `gain * (normalization * 0.99 / threshold)` to float once. The hot-loop
  threshold remains float. Regression `rvox.safety makeup` checks four exact
  float coefficients (e.g. -18/-3dB -> `11.112653732299805`); it failed before
  the change (`11.112654685974121`).
- **Numeric domain (replacement-only policy, not vendor parity):** nonfinite
  input samples (NaN, +/-Inf) are processed as silence, and allpass detector
  state saturates at +/-FLT_MAX. Before this, `FLT_MAX,-FLT_MAX` overflowed the
  detector, produced `Inf*0 = NaN` in the rational target and reached undefined
  float-to-integer conversions (UBSan `float-cast-overflow`, halting), then
  inverted continued gain. Both measures are identities for every finite
  ordinary sample, so >0dBFS audio is not clipped (regression checks +18dBFS).
  Output can still overflow to +/-Inf only if `|delayed sample * gain|` exceeds
  float range; that is defined IEEE arithmetic, not UB.
- Regressions cover finite extremes, NaN, +Inf, -Inf, continued valid audio,
  reset and mono/stereo at unity and -18dB thresholds, run under
  `-fsanitize=undefined,float-cast-overflow,address -fno-sanitize-recover=all`.

Existing captures re-rendered once at the changed source (no original-host run,
no tuning, unchanged thresholds; held-out evaluated only as final validation):

| Capture | Max abs | RMS | Tight 1e-6 / 1e-8 |
|---|---:|---:|---|
| mono 48k changing level | 7.599592e-6 | 1.211001e-7 | FAIL (worst frame 34559, 141 samples >1e-6) |
| stereo 48k | 3.159046e-6 | 1.352432e-8 | FAIL |
| deeper 48k | 4.470348e-6 | 5.720522e-8 | FAIL |
| 96k | 2.980232e-7 | 9.500499e-9 | pass |
| 44.1k stereo | 1.192093e-7 | 5.106885e-9 | pass |
| held-out speech (validation only) | 0 | 0 | pass (bit-identical) |

The makeup fix removes the RMS error floor at 96k/44.1k, but it does **not**
explain the 48k changing-level transient (~7.6e-6), which remains an unlocalized
residual. Reporting guards remain reporting-only, not equivalence proof.

## Reproduce portable checks

```sh
cmake -S plugins/rvox -B build-rvox -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Werror'
cmake --build build-rvox -j1
ctest --test-dir build-rvox --output-on-failure
```

Observed (historical, before `rvox.ceiling`): **5/5 CTest targets passed** (including `rvox.safety`); warning-clean GCC11 C++17 build. Separate
AddressSanitizer/UndefinedBehaviorSanitizer execution of the rate/channel/reset/
block/in-place/linked-chain test also passed. Metadata generation and generated
`PluginSpec.hpp` syntax compilation passed against the shared parameter contract.

`rvox_render input.raw output.raw channels rate compression gate gain` reads
interleaved little-endian float32 and returns latency-compensated output of the
same shape. It rejects partial frames and nonfinite control text.

An external original capture can be checked without distributing it:

```sh
python3 tests/rvox/compare_oracle.py original-output.npy candidate.raw \
  --channels 1 --max-abs 0.00001 --rms 0.0000002
```

See `comparison-summary.json` for machine-readable numeric receipts. The private
campaign owns full raw/oracle provenance and logs.

## Remaining acceptance blockers

- Residual changing-level transient error; full physical-control/automation,
  rate/block/channel and sustained-state oracle matrix is not exhausted.
- Hidden gate-release preset changes are not exposed/restored; only the recovered
  fresh default is implemented. Parameter display quantization/state integration
  require hosted adapter verification.
- RVox is registered plugin-locally (`plugins/rvox/tsunami.cmake`, `TSUNAMI_PLUGIN_ID=rvox`)
  as product **Tsunami Voice** (targets.json identity; plugin code TsRV) and hosted by the
  shared adapter as VST3/CLAP with an independently drawn product editor
  (`plugins/rvox/Editor.hpp`; Linux pluginval screenshot `hosted-editor-linux-pluginval.png`,
  taken before the rename, checklist `ui-checklist.md`). The Energy meter and Total
  Compression display read existing core state through the shared `CoreMeterSource`
  bridge (merged main 094ec6d; required, asserted by `rvox.editor`) -- a project-defined
  tap, not proven original meter scale/ballistics, see `meter-tap.md`. The hosted adapter
  offers stereo and an opt-in 1-in/1-out mono layout (`supports_mono` in
  `parameters.json`, `rvox.adapter` mono checks); Dark/Legacy skins and the WaveSystem
  toolbar are not implemented.
- Output ceiling (round-2 task3): Gain is a ceiling only for <=0 dBFS input. A
  +20 dBFS sine at Comp -6 / Gain -28.7 peaks at -23.58 dBFS in the genuine
  original and identically in the core and hosted adapter (max_abs 1.49e-8,
  `thresholds.json` `hot_input_ceiling_vs_genuine`, test `rvox.ceiling` and
  `rvox.adapter`). The +14.7/+13.7 dB readouts in the pluginval screenshot are
  peak holds (reset only by click, per the guide) accumulated across pluginval's
  Automation/Fuzz tests, where Gain and Compression were randomized; -28.7 is
  only the final fuzzed Gain. The meter tap equals the true output buffer peak.
  A Compression decrease raises makeup immediately, so a 0 -> -36 dB step
  overshoots the ceiling by up to ~36 dB for a few ms; whether the original
  does the same is NOT verified (the oracle renders static controls only).
- Parent review, native real-time/automation/overload and full acceptance remain.
  Keep the target PR draft; do not mark verified, merge or publish a release.
