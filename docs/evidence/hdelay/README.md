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

## Not implemented / blockers
BPM/Host sync, Analog Mod1-4 (kernel shows LCG noise + 3 double-precision sections + saturator), LoFi
(decimating FIR into reduced-rate line), modulation rate/depth, HiPass/LoPass (two biquads after read),
Phase L, Stereo / Mono-Stereo components (HBDelaySProc/HBDelayXProc), feedback >100 limiter, fractional
delays (rounded), mix law (linear, only 100 measured), output gain (only 0 dB measured), rates other than 48 kHz.
Coefficient computation lives in the Windows H-Delay.dll and was not recovered.

## TDD / commands
RED (stub Processor): `FAIL 375 ms tap centred at sample 18000` (ctest: 1 - hdelay_processor (Failed)).
GREEN: `cmake -S plugins/hdelay -B build && cmake --build build && (cd build && ctest --output-on-failure)` ->
`100% tests passed, 0 tests failed out of 1` (PASS single tap / feedback repeat / mix/output).
Fidelity: `python3 tests/hdelay/compare_oracle.py build/hdelay_render` -> `"pass": true` (needs private evidence).
