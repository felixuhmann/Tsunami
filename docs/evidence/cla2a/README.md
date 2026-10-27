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

## Not recovered (blockers)
* Coefficient initialisation (Windows init): curve tables, attack tables, release poles,
  Peak Reduction drive, Gain makeup, sidechain / programme-path filter cascades, Hi Freq
  network, auto-makeup, stereo link. All `Provisional::` values in `Processor.h` are
  placeholders (neither recovered nor fitted).

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
