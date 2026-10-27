# RVox Energy / Total Compression meter tap

Scope: project UI plumbing for the existing authored core. This is not a
comparison against the original plugin and does not claim original meter
parity.

## Definition (project-defined, existing-state tap)

`Processor::meterCount == 2`, `float Processor::meter(unsigned) const noexcept`
(`plugins/rvox/Processor.h`). Both read state that `process()` already computed;
`process()` itself is unchanged.

| Index | Name | Source state | Unit / range |
|---|---|---|---|
| 0 `kEnergy` | Input energy | `gateEnvelope_`: peak of input and allpass-rotated detector taps, instant attack, hidden 130 ms gate release | linear full scale, finite, >= 0 (can exceed 1 for >0 dBFS input) |
| 1 `kTotalGain` | Total gain before makeup | `accumulator1_` scaled by `4.656613e-10 * normalization_`: the lookahead-smoothed `gate*target` gain the next makeup multiply uses, aligned with the delayed output | linear, finite, 1 = no reduction, 0 = fully gated |

Idle values: after `prepare()`/`reset()` energy 0 and total gain 1; unprepared
core total gain 1; out-of-range index 0. Values are the state after the last
processed sample of a block.

The shared adapter bridge (`CoreMeterSource`, PR #23 at `3b64a12`) publishes
them with relaxed atomics after each `processBlock`. No callbacks, locks,
allocation or I/O on the audio thread. The editor polls at 30 Hz on the message
thread:

- Energy meter: `(dB + 80) / 80` of the left track (labels 12-72 dB below full
  scale), value readout in dBFS (`-inf` at or below -80 dB). No extra ballistics
  beyond the core envelope.
- Total Compression: `-20 log10(total gain)` drawn down from the top of the
  display on its 40 dB scale. The number below is the held maximum, and a click
  resets it.
- The bridge reads 0/0 before the first published block. The editor treats that
  pair as "no data". A fully gated silence that publishes exactly 0/0 also shows
  as no new data.
- After a host `reset()`, the bridge keeps the last published values until the
  next block (adapter behaviour, PR #23).
- Without the bridge (shared adapter before PR #23) the editor detects the
  incomplete `CoreMeterSource` type at compile time. Both areas stay drawn
  without data. `rvox.editor` prints `SKIP core meter bridge` for that build.

Not established: the original plugin's meter scale, ballistics, peak-hold
behaviour and whether its Total Compression includes the gate. These remain an
open reference-parity boundary.

## Evidence (Linux, xvfb, cached JUCE objects)

- RED: `tests/rvox/test_meters.cpp` failed to compile against the unmodified core
  (`meterCount`/`meter` missing). The editor checks 70-77 failed to compile
  against the unmodified editor (`energyLevel`/`reductionDb` missing).
- GREEN core (`rvox.meters`): idle/silence, reset, finite per block, -24 dB
  threshold on a 0.5 sine gives total gain < 0.2 and matches the core's rational
  law at the detector level, energy differs from output peak, gate -20 dB closes
  at -60 dBFS and opens at -6 dBFS, mono tap.
- GREEN editor (`rvox.editor` against PR #23 shared): 60 processed blocks then
  timer-only polling. Energy readout -6.0 dB, Total Compression ~18.5 dB,
  held-peak readout, input energy differs from output peak, 2 s silence empties
  energy and releases compression, click resets the hold.
  `editor-live-meters-linux.png` is that live state.
- Output regression (`rvox.output_regression`): FNV-1a hashes of rendered output
  for 8 stored cases (44.1/48/96/192 kHz, mono and stereo, varied controls,
  irregular block sizes including hot >0 dBFS and gated-tail segments) were
  captured from the unmodified `ae4773d` core at -O0 and -O2 (identical) before
  any change. They still match bit for bit. This is project-regression evidence
  only, not a new comparison against the original plugin.
