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
- r2 (Sync ms from 1/4 @120, fb 25, `20261027T161352-b7e2...`): delay law PASS (peaks 24000/48001 exact). CORRECTED
  ATTRIBUTION (independent review): the largest error (2.84e-2) is at REPEAT 1 (sample 312001), not repeat 2. At 500 ms the
  genuine echo is a clean single sample (exactly 1/9 = 0.125 x 8/9, then 1/37) with no side lobes, while the core applies
  the 31-tap kernel measured at 375 ms. The fixed-kernel assumption is falsified by held-out data; the side lobes seen at
  375 ms and 250 ms are probably a fractional-read/time-quantisation artifact (cause unresolved). Still FAIL on max|err|
  after task3 (DC errors now 1e-6 / 8.7e-5).
Round-1 fb0/fb50 comparisons are CIRCULAR (calibration renders), marked so in `oracle-comparison.json`.

## Round 2 task3 (thresholds `thresholds.json#round2_task3`, declared 16:18Z before rendering)
Renders (250 ms via delay_bpm 1/8 + Sync ms, impulse 0.125 at 6.0 s): fb50 `20261027T161809-a717...`, fb150
`...161833-1602...`, fb90 `...161848-3fa0...`, HP raw 0.3 `...161917-f3d9...`, LP raw 0.3 `...161945-d951...`,
HP raw 0.7 `...161959-6b5d...`, LP raw 0.7 `...162013-5ece...`.
- Exports checked first (H-Delay.dll decompile): FUN_180033430 writes a coefficient value x 0.01 x scale (linear
  percent law); FUN_1800348c0 is a one-pole smoother coef = e^(-1/(fs*T_ms*1e-3)); FUN_18002c740 is a log taper
  p0 x (p1/p0)^x. No biquad formula / Q constants were found in the exported functions; HP/LP form is behavioural.
- Feedback: genuine repeat ratios rise toward fb/100 as level falls (fb50: .4732 .4868 .4935 .4968 .4984). Model:
  line = s(x + fb/100 x v), s(u) = u/(1+|u|), unity-DC read. This also explains the old 0.88892 "tap DC" (= s(0.125)/0.125)
  and the exact 1/9, 1/37 at 500 ms. Held-out (`oracle-comparison-r3-feedback.json`): fb90 r2/r1 rel err 2.1e-3,
  r3/r2 3.5e-3 (<=5e-3) PASS; fb25 r2/r1 8.6e-5 PASS. CAVEAT: a polynomial model (s - 0.9 s|s| + 0.65 s^3) was frozen
  first on fb50+fb150 and passed fb90/fb25 (2.7e-4/1.9e-3, 3.7e-4); the implemented u/(1+|u|) form was then read from
  the exact rational values of the fb25 render, so fb25/fb90 are not strictly held-out for the implemented form (it has
  no fitted parameter). fb150 repeats >= 3 diverge (limiter not recovered). Saturator bounds fb up to 200 (TDD).
- Glide: tau 0.5005 s fitted to BPM90 render 154836 (echo residuals <= 0.4 sample); implemented T = 0.5 s,
  per-sample exponential, linear interpolation between integer kernel reads while gliding. Check vs `160946`
  (375->500 ms, same input): genuine 21555, core 21556 (+-3 PASS; consistency check, same glide as fit render).
- HiPass/LoPass: raw 0.49266 reads back 588 Hz (task2 mislabelled it "1.00kHz"); raw 0.3 -> 154 Hz, 0.7 -> 2.49 kHz;
  taper 19 x (20000/19)^(raw x 818/817). RBJ-form 2nd order: Q fitted on 154/588 Hz: HP 0.80, LP 0.949
  (fit err <= 0.05 dB). Held-out 2.49 kHz (`oracle-comparison-r3-filters.json`): HP 0.19 dB, LP 0.19 dB (<= 0.5) PASS.
  Placement (wet path, outside the loop) PROVISIONAL.
- Stereo component parameter list (oracle render readback `20261027T100739-5107...`, H-Delay Stereo): Mono list plus
  pingpong {Stereo, PingPong}, phase_r {+,-}, ping_pong_start_direction {Left, Right}, delay_sec_r (1-3500 ms, default
  375), delay_bpm_r (20 notes, default 1/8D). Order: bypass, delay_sec, delay_bpm, feedback, pingpong, sync, mix, output,
  analog, lofi, rate, depth, hipass, lopass, phase_l, phase_r, bpm, ping_pong_start_direction, delay_sec_r, delay_bpm_r
  (then MIDI CC pseudo-parameters). Mono/Stereo: blocked (host: 1-in/2-out bus unsupported by the oracle path).

## Not implemented / blockers
Host-tempo input, Analog Mod1-4 (kernel shows LCG noise + 3 double-precision sections + saturator), LoFi
(decimating FIR into reduced-rate line), modulation rate/depth, read-kernel/fractional-delay law (500 ms clean vs
250/375 ms side lobes),
Phase L, Stereo / Mono-Stereo components (HBDelaySProc/HBDelayXProc), feedback >100 limiter (saturator bounds it), fractional
delays (rounded), mix law (linear, only 100 measured), output gain (only 0 dB measured), rates other than 48 kHz.
Coefficient computation lives in the Windows H-Delay.dll and was not recovered.

## TDD / commands
RED (stub Processor): `FAIL 375 ms tap centred at sample 18000` (ctest: 1 - hdelay_processor (Failed)).
GREEN: `cmake -S plugins/hdelay -B build && cmake --build build && (cd build && ctest --output-on-failure)` ->
`100% tests passed, 0 tests failed out of 1` (PASS single tap / feedback repeat / mix/output).
Fidelity: `python3 tests/hdelay/compare_oracle.py build/hdelay_render` -> `"pass": true` (needs private evidence).

Round-2 task3 TDD: RED `FAIL repeat ratio rises toward fb/100 as level falls`; compile-RED `saturate`/`filterCutoffHz`
missing; RED `FAIL HiPass 154 Hz: -15.47 dB at 62.5 Hz`; GREEN `hdelay_test` all PASS (single tap, feedback repeat,
mix/output, sync, glide, feedback bounded/saturator, hipass/lopass). Fidelity:
`python3 tests/hdelay/compare_oracle_r3.py build/hdelay_render` (pass) and `compare_filters_r3.py` (pass);
`compare_oracle_r2.py` still FAILs r2 max|err| (read-kernel issue above). Unverified extrapolations: BPM->ms copy,
the unrounded copy, triplet factor 2/3, 1B/2B = 4/8 beats, Host tempo 120.

## Round-2 task4: read-phase law (thresholds `thresholds.json#round2_task4`, declared and committed d59a5dc before renders)
- Export grounding (existing decompile `rea/kernels/functions/0x101100.c`, HBDelayProcMono): the line read is a
  32-phase polyphase interpolator. The phase index is `round(32 - frac*32)`, with 0x80-byte (32-float) phase rows.
  The read pointer advances at rate `clamp(measured_distance / target, <=2)`, which is a self-correcting glide.
- Genuine echo kernels fit `sinc(n - 1/32)`, i.e. one phase step: 250 ms (161055) and 125 ms (new, 162857) both give
  residual 2.08e-3, the same value. The 500 ms echo reached by an UPWARD glide (161352) is one clean sample.
- `compare_read_phase_r4.py` -> `oracle-comparison-r4-readphase.json`: down/125 ms vs sinc(n-1/32) 2.08e-3 <= 5e-3
  PASS. Held-out up case (delay_bpm 15/19, 162912) FAIL: the genuine echo was still gliding at 6 s (peak 31967 of
  32000, free fractional phase 0.36), so it did not test the settled phase.
- Implemented (TDD; RED `FAIL upward-settled 500 ms echo is a clean single sample`): when approaching from below
  within half a phase step (1/64 sample), read the integer sample. Otherwise use the +1/32 31-tap row. PROVISIONAL:
  the float read-pointer law is not reproduced.
- r2 per-sample check (unchanged thresholds) still FAILs: max|err| 2.84e-2 at 24001. The core's exponential glide
  (T = 0.5 s) is still 0.037 sample short at 6 s (more than 1/64), while the genuine one has converged. At 666 ms the
  genuine glide was 33 samples short at 6 s, which an exponential with tau = 0.5 s cannot give. So the glide law
  (export rate law) is the open item, not the kernel.
- Lead for the >100% limiter (not implemented): in export 0x101100 the write gain is
  `g = min(K/(|u|+K), (1-r) + r*g_prev)` and the line gets `g*u`. With K = 1 this is the implemented u/(1+|u|), plus an
  instant-attack / one-pole-release gain smoother with unknown r. Next: fit r on fb150 and test on fb110/fb200.
