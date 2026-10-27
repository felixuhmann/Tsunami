# RComp: makeup-integrated mono wet path (partial)

**Partial candidate wet path, not a Processor.** [`WetPath.h`](../../../plugins/rcomp/WetPath.h)
connects `HistoryGeometry` (rate band, spans, shift, normalization) to `GainHistory`
and applies the reference kernel's makeup multiply. Given a unit-domain computed
gain and input audio it produces makeup-scaled, `H`-sample-delayed wet audio.

## Evidence used (no vendor text reproduced)

- Auxiliary ELF `ProcessFuncM` (SHA256 `95a060dc…3822`), instruction level: the
  smoothed history gain is multiplied by float control `+0x44` times
  `float(1 << int control +0x50)`. That product goes through two state values
  (multiply, then add) and is then multiplied by the delayed audio sample. Control
  `+0x48` is subtracted from the smoothed gain only on a meter path.
- PE makeup writer `0x18002cef0` (SHA256 `4ecaafa3…ac79`) writes three consecutive
  entries: the mantissa, `inverseNormalization·0.0625`, and the integer
  `exponent+4`. The 4-byte writer stride puts them at `+0x44`, `+0x48` and `+0x50`.
  So `+0x44·2^{+0x50}` = `mantissa·2^(exponent+4)` = `16·Gain·normalization`.
- Steady-state history output for constant input `u` is `u·inverseNormalization`.
  Unity physical makeup therefore requires the history to hold unity gain at 1/16
  scale. Two independent facts agree with this: the startup prefill constant
  0.0625 (`0x180029540`) and the unity meter reference `+0x48` = `inverseNormalization·0.0625`.

## Implemented

`tick(g, x) = (smoothed(g·0.0625) · k) · x[n-H]` with `k = float(mantissa)·float(2^(exponent+4))`.
The two state values are assumed to be at their identity values (scale 1, offset 0).
That matches the original-kernel probe configuration; their automation-smoothing
writer is not resolved. `GainHistory::Frame` additionally exposes the delayed
sample. Existing outputs are unchanged.

## Tests (native Linux, GCC 11.4, Release -Werror; Debug ASan+UBSan+float-cast-overflow)

`tests/rcomp/test_wet_path.cpp`, at 44.1k/96k/192k and Gain {0, 6, -12.5, ±30} dB:
- exact kernel operation order against an independently stepped `GainHistory`;
- unity steady state equals `dB→linear(Gain)·x[n-H]` within 2e-6 relative;
- atomic rejection of invalid rate/makeup;
- reset.

RED (missing header) → GREEN 5/5; sanitizer 2/2 (wet_path, gain_history).

## Not claimed / remaining

There is no new original-host or kernel differential capture for this composition.
Both steady-state agreement and operation order are analytic or instruction-derived.
Also still missing:
- the detector/gain computer coefficient mapping (`GainStage` is in coefficient units);
- the soft-gain scale/offset (`+0x3c`/`+0x38`) writer;
- ARC (`0x18002d7d0`);
- the nonzero startup prefill storage format (the ring holds integer gain words and float audio);
- state `+0x5c/+0x60` smoothing;
- stereo/modes, the Processor, UI, and native hosted Windows/Linux acceptance.

When `WetPath` carries Gain, `OutputStage` must be used at 0 dB Gain so that makeup is not applied twice.
