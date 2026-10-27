# H-Delay (Tsunami Echo) evidence - PARTIAL, NOT VERIFIED

Reference: Waves H-Delay 17.1.42.50, component "H-Delay Mono", private oracle (Wine/Pedalboard), 48 kHz, block 512.

## Implemented segment (grounded)
- Free-time delay line, feedback, mix, output, bypass (`plugins/hdelay/Processor.hpp`).
- Structure from XLMC symbol `HBDelayProcMono` (Ghidra pseudocode, private): line write = input + feedback state
  from the previous sample; 31-tap band-limited (32-phase polyphase) read; feedback state = scaled wet output.
- Wet tap response (31 samples, DC 0.88892, peak 0.88748) measured from genuine impulse render
  `20261027T100439-e9f996eeafa34fa1aa58d94662f6b2a7` (Analog Off, HiFi, fb 0, mix 100, 0 dB, HP/LP Off). Behavioural
  measurement; no vendor code or tables copied.
- Loop: genuine fb=50 render `20261027T153826-4ddf375da6324f8bafda1a72c0652ec1` shows repeat 2 peak at 36001
  (loop = delay + 1 sample, matching the kernel's next-sample feedback state) and repeat2/repeat1 DC = 0.473202
  = 0.5 x 0.946403 (calibrated constant `kLoopDc`).

## Important oracle finding
Both genuine renders requested `delay_sec`=100 ms (first render) or left it at default, with `sync` raw 1 (readback "ms"),
yet the tap is at 18000 samples = 375 ms = 1/8D at 120 BPM (fresh-instance Host sync default). The delay-time
parameter did not take effect in the oracle path; delay-time mapping (raw->ms, sync modes) is therefore UNVERIFIED.
Comparisons below use 375 ms in the core to match what the genuine plugin actually rendered.

## Comparison (thresholds predeclared in tests/hdelay/compare_oracle.py before running)
Thresholds: tap peak index exact; per-sample max |err| / input peak <= 2e-3; per-repeat DC rel. error <= 5e-3.
- fb0_375ms: exact (0.0) - CALIBRATION CASE, circular by construction, not a fidelity claim.
- fb50_375ms: peaks 18000/36001 exact; repeat-2 DC rel err 5.8e-7 (gain calibrated from this render);
  repeat-2 shape (prediction kTap*kTap) max |err| 6.87e-4 normalized -> PASS but gain is not held-out.
Report: `docs/evidence/hdelay/oracle-comparison.json`.

## Round 2: delay-time control resolved (oracle path)
Cause: the host sets controls in one block and the plugin receives them in parameter-index order
(delay_sec idx 1, delay_bpm idx 2, sync idx 4). Switching Sync Host->ms copies the current note-derived time
(delay_bpm note x 60/120) into delay_sec, overwriting a same-block delay_sec write. In addition, genuine delay
changes GLIDE from the fresh-instance 375 ms (fits an exponential with tau ~0.49 s: BPM 90 1/8D render first/second
echo at 21555/44620). Workaround without touching oracle.py: choose the time via delay_bpm (+ Sync ms) and place the
impulse at 6.0 s so the glide has settled. Diagnostic `20261027T160946-1f2a...` (delay_bpm 1/4, delay_sec 100, ms):
echo matches the 375->500 ms glide, not 100 ms.
Implemented (TDD): `effectiveDelayMs()` = note beats x 60/tempo in BPM/Host (BPM raw r -> 30+270r from readback;
Host tempo 120 PROVISIONAL; "1B"/"2B" = 4/8 beats PROVISIONAL), Host->ms copy on Sync switch. Glide NOT implemented.
Held-out comparisons (thresholds predeclared in `thresholds.json`): see `oracle-comparison-r2.json`.
- r1 (Sync ms from 1/8 @120, fb 0, `20261027T161055-6502...`): PASS, peak 12000 exact, max|err| 1.27e-4.
- r2 (Sync ms from 1/4 @120, fb 25, `20261027T161352-b7e2...`): delay law PASS (peaks 24000/48001 exact) but
  loop gain FAIL: genuine repeat2/repeat1 = 0.24324 vs model 0.25 x 0.946403 = 0.23660 (dc rel err 2.7e-2 > 5e-3;
  max|err| 2.8e-2 > 2e-3). kLoopDc is not feedback-independent; feedback law is non-linear (unresolved; not tuned
  on this held-out render).
HP/LP (fit data only, not implemented): `filter-measurements-r2.json`, echo spectrum relative to unfiltered r1 echo.
HiPass "1.00kHz": -3.6 dB @500 Hz, -14.7 @250, -26.8 @125, -38.9 @62.5 (12 dB/oct, 2nd order), +0.14 dB @1-2 kHz.
LoPass "1.00kHz": -8.3 dB @1 kHz, -21.1 @2k, -33.6 @4k, -47.0 @8k, +0.6 dB @250-500 Hz (2nd order, resonant,
bilinear-like steepening near Nyquist), echo peak shifted +16 samples. Cutoff/Q laws not yet derived (need >=2 more
cutoffs per filter, one held out).

## Not implemented / blockers
Delay-time glide, host-tempo input, Analog Mod1-4 (kernel shows LCG noise + 3 double-precision sections + saturator), LoFi
(decimating FIR into reduced-rate line), modulation rate/depth, HiPass/LoPass (two biquads after read),
Phase L, Stereo / Mono-Stereo components (HBDelaySProc/HBDelayXProc), feedback >100 limiter, fractional
delays (rounded), mix law (linear, only 100 measured), output gain (only 0 dB measured), rates other than 48 kHz.
Coefficient computation lives in the Windows H-Delay.dll and was not recovered.

## TDD / commands
RED (stub Processor): `FAIL 375 ms tap centred at sample 18000` (ctest: 1 - hdelay_processor (Failed)).
GREEN: `cmake -S plugins/hdelay -B build && cmake --build build && (cd build && ctest --output-on-failure)` ->
`100% tests passed, 0 tests failed out of 1` (PASS single tap / feedback repeat / mix/output).
Fidelity: `python3 tests/hdelay/compare_oracle.py build/hdelay_render` -> `"pass": true` (needs private evidence).
