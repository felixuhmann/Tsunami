# DeEsser (Tsunami De-Esser) evidence

Status: portable DSP core functionally matched on every stored genuine render case below. **Partial target**: no editor/UI, no foundation (root CMake/plugin wrapper) integration, parameter automation within a block is not modelled; block-boundary changes are tested on the Tsunami core only (no genuine automation oracle). Not marked verified.

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
Meters: `Processor::gainReductionDb()` (largest attenuation in the last block, positive dB) and `detectorLevelDb()` (peak detector envelope, dBFS) are lock-free relaxed atomics written once per `process()`; audio output is unchanged (stored genuine comparison re-run: all PASS).

## Remaining limitations
- Parameter changes mid-stream not compared to the genuine plugin (oracle has no automation path); intra-block (sample-accurate) changes not modelled.
- Sample rates outside 44.1/48/96 kHz, and Freq near 16 kHz at 44.1 kHz (Nyquist clamp path), not compared.
- No editor/UI or host wrapper; root build integration belongs to the foundation worker.
