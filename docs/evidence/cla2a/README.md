# CLA-2A (Tsunami Opto) evidence summary

Status: **structure recovered, coefficients provisional, wet output NOT matched.**

## Recovered from the reference (independently re-expressed, no vendor code/tables)
Source: unstripped Linux kernel symbols in the genuine bundle (Ghidra export, private analysis).

| Segment | Source file | Test |
|---|---|---|
| Detector is full-wave `|x|` | `Kernel.h detect` | `test_kernel` |
| Static gain: 26-point piecewise-linear lookup, clamped, input floored at 0; Comp/Limit select different tables | `interpolateCurve` | `test_kernel` |
| Program-dependent attack: 12-point descending table on clamped `level*sens`, both poles interpolated, weight 0.75 | `attackPoles` | `test_kernel` |
| Constant release poles; attack branch only while target < memory sum | `Ballistics` | `test_kernel` |
| Two-memory gain recurrence, 0.75/0.25 weighting, unity init | `GainMemory.h` | `test_memory` |
| Gain-dependent cubic blend polynomial, clamp [floor,1] | `BoostPolynomial` (not yet wired: path filters unrecovered) | `test_kernel` |
| Analog hum: renormalised quadrature oscillator | `HumOscillator` | `test_kernel` |
| Input noise: wrapping 32-bit LCG, two draws, sum/diff pair, injected at 1e-8 | `NoiseSource` | `test_kernel` |
| Mix / output gain one-pole smoothed | `Smoother` | `test_kernel` |

## Control mapping (genuine host enumeration)
`ControlMapping.h` maps Gain / Peak Reduction knob values to normalized. A 1000-step
normalized sweep at 0.04 display resolution reproduces all 929 (Gain) and 931 (Peak
Reduction) distinct host display values, plus the defaults 40 -> 0.535 and 46 -> 0.598
(`test_mapping`).

## Not recovered (blockers; updated round 2 task 3)
* Recovered since round 1: Peak Reduction drive, Gain makeup, auto-makeup table
  (`ControlLaws.h`) and the 48 kHz three-biquad cascade design (`SidechainFilters.h`).
* Characterized (fitted, not recovered): Comp/Limit static curves (`CharacterizedCurve.h`)
  and opto time constants (see round 2 task 3 below).
* Still not recovered: reference curve breakpoints, attack table / release pole values
  (hidden-control defaults), Hi Freq network, stereo link, coefficient-id -> kernel map.

## Genuine-render comparison
Thresholds predeclared in `thresholds.json` before comparison; results in `comparison.json`.
* Dry subset (mix 0, trim +6 dB): max abs error 1.19e-7 <= 1e-6 -> **PASS**.
* Wet defaults (tone_plus_noise, stereo_independent): rel. RMS error about -2 dB, steady
  level about -14 dB vs reference -> **FAIL** (expected; provisional coefficients).
* Host finding: the genuine host decodes int16 WAV as v/32767.

Restricted kernel tests and the complete-plugin fidelity results are separate claims.
No complete-plugin match is claimed.

## Round 2 (coefficient recovery from existing exports)
* Coefficient writers in CLA-2A.dll go through `GetPointerToCoef(alg, inst, id)` + a store
  callback, with most values read from hidden per-instance control slots
  (`*(alg+0x598) + id*0x600 + 0xb0`). Those hidden-control defaults (curve / attack /
  release times, filter shapes) are not present in the acquired code exports, so the
  26-point curve, 12-point attack tables and release poles remain **not recovered**.
* **Recovered** (`plugins/cla2a/ControlLaws.h`): Gain knob -> makeup dB (11-pt table,
  FUN_18002fa10 + .rdata 0x18004b560), Peak Reduction knob -> drive dB (5-pt table,
  FUN_180030d80), auto-makeup compensation table (FUN_18002f3a0, .rdata 0x18004b3f0/
  0x18004b420; not yet wired). Control 38 = Gain / 74 = Peak Reduction is inferred from
  the auto-makeup coupling; kernel placement of the two coefficients is assumed.
* Wet defaults vs unchanged thresholds (`comparison-r2.json`): level diff -13.5 -> -6.1 dB
  and -14.3 -> -6.7 dB; rel. RMS error -2.0 -> -5.9 dB and -1.9 -> -5.4 dB -> still **FAIL**.
  No fitting on these renders.
* **Characterized** (not recovered; `CharacterizedCurve.h`, `tests/cla2a/fit_static_curve.py`):
  26-point Comp and Limit static gain-reduction curves vs detector dB, plus a -0.35 dB
  1 kHz programme-level offset, fitted to 4 new genuine stepped-tone renders (PR 25/46/70
  Comp, PR 46 Limit). PR46 and PR70 Comp data coincide at equal (input + drive) level,
  supporting the drive-law placement. Attack/release ballistics remain PROVISIONAL.
* Wet defaults with characterized curve (same thresholds): rel. RMS error -14.1 / -19.0 dB,
  level diff +1.60 / +0.96 dB -> still **FAIL** (thresholds -40 dB / 0.5 dB).

## Round 2 task 3 (filters, ballistics)
* **Recovered design** (`SidechainFilters.h`): FUN_180031a10 builds three analog
  second-order prototypes from literal constants (48 kHz: 60 Hz/Q0.5/-2,+6.5,0 dB;
  9932.4 Hz/Q0.4707/+0.2,+5.3152,+1.3328 dB; 722 Hz/Q0.404/0,-0.3,+0.1 dB), bilinear
  via FUN_1800094a0 prewarped at 50 Hz; FUN_180030480 chains them. `test_filters`.
* **Placement inferred**: genuine PR 0 multitone (161747) gives a flat programme path
  (8.43..8.51 dB, 40 Hz..2.5 kHz; -0.6 dB at 10 kHz) while the cascade varies ~6 dB,
  so it is applied to the detector input. Genuine frequency steps at PR 46 (161915):
  100/250/1000 Hz within 0.5 dB (PASS); 4 kHz +1.39 dB, 10 kHz +2.92 dB (FAIL: more
  genuine HF detector emphasis, Hi Freq network not ported). Thresholds `r3_*` in
  `thresholds.json` were declared before these comparisons.
* Render use disclosure (runtime/evidence/cla2a):
  - fitting: 161007, 161031, 161139, 161215 (static curve, round 2); 161805 (PR 46) and
    161819 (PR 70) step bursts (time constants).
  - verification only (not fitted): 161747 (multitone PR 0), 161915 (frequency steps).
  - evaluation only (never fitted): 095837, 095855, 095937 (dry / wet defaults).
  - held out: speech material, never rendered for fitting or comparison here.
* **Characterized time constants** (`Processor.h times_`, `tests/cla2a/fit_ballistics.py`):
  bounded fit on 161805/161819 step bursts; in-sample envelope RMS 0.12 / 0.30 dB. The search
  is ill-conditioned at the quiet end of the attack table. Genuine release has a slow
  multi-second tail (two-stage opto behaviour); 0.75/0.25 weighting kept from the kernel.
* Auto Makeup: genuine host default "Off ", so it is not wired for the defaults.
* Wet defaults (unchanged thresholds, `comparison-r3.json`): rel RMS -19.04 / -19.11 dB,
  level +0.91 / +0.94 dB -> still **FAIL**.

## Source maintenance: processor API/state lifecycle (no fidelity change)

Scope: existing project source and `docs/PLUGIN_CONTRACT.md` only; no vendor
binaries, exports, captures or fitting scripts were used. New test
`tests/cla2a/test_lifecycle.cpp` (ctest `cla2a_lifecycle`).

Demonstrated bugs fixed (replacement-only defensive policy, NOT
original-equivalence):

- `setTimeConstants` accepted NaN/inf/non-positive values. A NaN constant
  poisoned the ballistics memory so output stayed silent even after valid
  constants were restored (until `reset`). Updates with any non-finite or
  non-positive value are now ignored as a whole.
- Finite double results beyond float32 range were cast to `inf` (e.g. dry
  path, +18 dB trim, 1e38 input). Such samples are now written as 0, the
  same policy as the existing nonfinite-output guard.

RED: `cla2a_lifecycle` failed with `FAIL: invalid time constants rejected`,
then (after that fix) `FAIL: dry trim overflow stays finite`. GREEN: all 7
ctests pass. Regression guards (already passing, not new behaviour):
controls/time constants before prepare, repeated prepare/reset preserve
parameters, rejected prepare/nonfinite controls leave state intact,
zero-frame/null-output calls, in-place equals out-of-place, unprepared
process writes silence. Unprepared silence arises through the existing
nonfinite-output guard (default-constructed curves); `prepare` clears it.

Bit-identity: a 75-configuration render (5 control sets x 44.1/48/96 kHz x
5 levels up to +12 dBFS, 333-frame blocks) is byte-identical before and
after the change (sha256 f3a90216...0910154).

CI: `.github/workflows/cla2a-core.yml` runs these portable partial-core tests
on native ubuntu-22.04 and windows-2022. A green run is NOT plugin fidelity,
host, UI or release acceptance.

Unchanged open gaps: Hi Freq network not ported (4/10 kHz sidechain points
still fail), default gain offset/levels and wet defaults UNMATCHED, opto
time constants only characterized, stereo link/level behaviour unverified,
no plugin wrapper/UI, no native host/plugin evidence.
