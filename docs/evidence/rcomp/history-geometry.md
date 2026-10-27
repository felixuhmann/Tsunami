# RComp: history geometry, startup prefill layout and makeup normalization

**Partial state API, not integrated and not a Processor.** [`HistoryGeometry.h`](../../../plugins/rcomp/HistoryGeometry.h)
adds independently written C++17 for the reference wrapper's sample-rate
dependent history geometry, its startup prefill layout and the makeup-gain
normalization split (RComp 17.1.42.50, PE SHA256
`4ecaafa3628a11c3e34e60c34182d7daabefadd3f231918fd47bcefe88d5ac79`, mono).
Grounding: private exports of the reference geometry writer (`0x18002ccd0`),
history initializer (`0x180029540`) and makeup coefficient writer (`0x18002cef0`);
no decompiler text is included here. `0x180028450` (ceiling/release/follower
state init) and the dispatcher `0x18002e4f0` were **not** used or resolved in
this slice; no REA/Ghidra or original-host execution was run.

## Recovered behavior implemented

| Integer rate (Hz) | H | log2 H | A (first rect.) | second rect. = H-A+1 | GainHistory middleSpan | shift = ceil(log2(A·second)) |
|---|---:|---:|---:|---:|---:|---:|
| 1..63999 | 64 | 6 | 27 | 38 | 11 | 11 (1026) |
| 64000..127999 | 128 | 7 | 54 | 75 | 21 | 12 (4050) |
| >=128000 | 256 | 8 | 108 | 149 | 41 | 14 (16092) |

- Mono shared history length `(outputs+2)·H+1 = 3H+1`, equal to the restricted
  GainHistory reduction `2H+2A+B`.
- `normalization = 2^shift/(A·second)`, `inverseNormalization` its reciprocal.
- Startup prefill layout: `[0,H)` zero, `[H,2H+1)` constant 0.0625, zero to the
  end. This is **nonzero**, unlike the existing `GainHistory` zero reset.
- Makeup: `scaled = dB→linear(Gain)·normalization`; repeated halving until
  `<=1`; if no halving occurred it is halved once anyway (exponent 1). So
  `mantissa·2^exponent == scaled`, `mantissa ∈ (0,1]`; shift compensation
  `exponent+4`; paired history coefficient `inverseNormalization·0.0625`; the
  writer also stores `scaled·16`.

## Exact coefficient domain and limits

- Rate is taken as a positive integer, as stored by the reference; band selection
  at 64000/128000 inclusive. Nonpositive rates are rejected (replacement policy,
  not recovered).
- Gain is validated to the physical ±30 dB range; dB→linear assumes the standard
  20·log10 amplitude convention (the reference conversion helper was not exported).
- The prefill constant and mantissa are **coefficient-domain values**. The two
  writer function pointers (zero vs 0.0625 writers, and the mantissa/shift
  writers) are indirect; their stored fixed/float representation and the exact
  buffer length (taken from an external buffer) are unresolved. The mapping of
  the prefill span onto `GainHistory`'s separate ring/audio delay is **not**
  established, so `GainHistory` is unchanged and still zero-resets.
- Not connected: GainHistory, detector maximum-history, gain law, ARC tables and
  OutputStage. Do not multiply GainHistory output by `OutputStage` makeup and this
  normalization together: the reference folds physical Gain into this split, so
  applying both would double-count makeup.

## TDD (witnessed, sequential)

Local GCC 11.4.0, CMake 3.22.1, Release, `-Wall -Wextra -Werror`, heavy-slot flock.

1. R1 (review): helper self-test added first → `comparison helper accepts
   nonfinite values`, ctest exit 8 (log sha256 `3b2cbc37…`). Helper changed to
   require finite operands → PASS (`9995fe83…`). R2 coverage (nontrivial
   Gain 4.5/Trim −2/Mix 60 with out-of-place, dry-alias, wet-alias blocks; twelve
   rejected updates with both paths active, state compared after all of them;
   defaults and endpoints) was added in
   the same GREEN run: it is new coverage, not a witnessed missing feature.
2. Geometry stage: test written first → compile failure `HistoryGeometry.h: No
   such file or directory` (`537a27e3…`). After implementation two runs failed on
   **test-oracle** float exactness (exact `norm·product == 2^shift`; and
   `(p·2048)/1026` vs `p·(2048/1026)` association), logs retained
   (`4a6342af…`, `e52ea101…`); the oracle was corrected, not the stage →
   `PASS rate bands and edges…`, `PASS invalid rate rejection`, `PASS makeup
   mantissa/exponent normalization split`.
3. ASan+UBSan (`-fno-sanitize-recover=all`) on both changed tests: 2/2 passed
   (`3b87dfee…`).

The band table in the test is read from the reference writer; analytic checks
independent of the implementation arithmetic: `second = H-A+1`, ring identity,
`2^(shift-1) < product <= 2^shift`, reciprocal normalization, exact
`ldexp(mantissa, exponent) == scaled`. Unchanged GainStage/GainHistory tests,
the 320-case kernel differential and foundation gates were not rerun; no new
original captures were made.
