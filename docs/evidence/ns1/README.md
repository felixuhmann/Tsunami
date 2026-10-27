# NS1 (Tsunami Suppressor) evidence: PARTIAL (mono + linked stereo)

Independent GPL-3.0-or-later C++17. It has no framework/UI dependency and loads no vendor code. Not verified, and no general equivalence claim is made.

## What exists

- `detector.{hpp,cpp}`: the typed adaptive band gain estimator (interband floor ratio, asymmetric smoothing, fourth-power threshold followed by squared shaping, mixed float/double boundaries).
- `mono.{hpp,cpp}`: the mono chain. It has a two-stage parallel allpass front, six detector bands, a global activity latch, six three-stage audio sections, and `configure()` to swap coefficients while keeping state.
- `design.{hpp,cpp}`: independent trigonometric coefficient design plus the initial detector state, for the full NS1 range via `controlParameters()`. The 24 initial-state constants (routine 0x180026950) and the two activity-correction constants in `mono.cpp` (0.000275786, 44.12576; kernel 0x1014b0) were recovered from the vendor binary analysis; they are recovered constants, not semantic values.
- `Processor.hpp`: the plugin contract (`prepare/reset/setParameter/getParameter/process/latencySamples/tailSeconds`). Index 0 is `NS1` (0..100 %, default 0). Index 1 is `Bypass` (sample-exact identity).
- `parameters.json`: taken from the reference parameter readback of a fresh instance (`NS1` 0..100 % step 0.1 default 0.0, `Bypass` bool default Off).

## Limitations (explicit)

- **Continuous control law (round 2).** `controlParameters()` is piecewise linear with breakpoints 0.2/0.8, with front edge frequencies at 1 Hz resolution (see `control-law-derivation.json`). The predeclared 0.25/0.75 gate passes (16/16 rows, worst nrmse 1.2e-7; `offgrid-comparison.json`). The [0,0.2] and [0.8,1] segments rest on one oracle render each (10 %, 90 %). With the oracle's int/32767 PCM16 input decoding (`derivation-pcm16-32767.json`), chirp@10 % FAILS (max 1.6e-4 > 1e-4) and the 90 % rows are DEGRADED (pass, but 30-80x above the ~1e-6 interior error floor); chirp@62.5 % passes (max 8.1e-5). Held-out 5 %/95 % oracle renders (`heldout-outer-comparison.json`, thresholds predeclared, no tuning): white_noise@5 % PASS (nrmse 1.1e-6); chirp@5 % FAIL (max 2.6e-4); white_noise@95 % FAIL (nrmse 2.3e-3, max 4.1e-4); chirp@95 % FAIL (nrmse 4.2e-3, max 5.3e-3). The outer segments are therefore NOT confirmed; [0.8,1] is wrong and [0,0.2] has a chirp residual. `controlEstablished()` still reports only 0/50/100 %. The 25 % point is not independent: factory presets at 25/30/50/80 % were derivation data (amendment in `thresholds.json`); 75 % is the held-out gate point.
- **Linked stereo (round 2).** With 2 channels, `Processor` runs one shared detector driven by the mean of both channels' front outputs, with per-channel front and audio sections. It passes the predeclared stereo thresholds against the fresh oracle `stereo_independent` @ 50 % render (nrmse 3.4e-5) and all 10 calibrated NS1 Stereo captures, including left-only, right-alone, identical and swapped inputs (worst nrmse 9.4e-6). See `stereo-comparison.json`. Other channel counts are refused.
- `prepare()` throws for sample rates outside 40-100 kHz (the design is only validated in that range).
- Parameter changes swap coefficients instantly. The reference's control smoothing/transition behaviour is unmeasured.
- Bypass does not advance DSP state. The reference's behaviour on bypass toggles mid-stream is unmeasured.
- Latency is reported as 0. The impulse response starts at sample 0 in the reference captures, and the host-reported latency is unknown.
- Tail is reported as 0.5 s, a conservative figure. The measured impulse decay below 1e-6 takes 0.025-0.051 s at 44.1/48/96 kHz.
- Results are from GCC 11 / x86_64 with `-ffp-contract=off`. Other compilers/FMA are unvalidated.

## TDD

- RED: `tests/ns1/test_processor.cpp` was written first. It failed to compile with `fatal error: Processor.hpp: No such file or directory`.
- GREEN: after adding `Processor.hpp` and `Mono::configure`, `processor contract checks passed`.
- The prior worker's design, initialized and mono tests were verified to pass before integration.

## Full-plugin fidelity vs genuine original renders

Thresholds were declared in `tests/ns1/fidelity_thresholds.json` before comparing. A row passes if nrmse <= 1e-3 and max |err| <= 1e-4.

There is one amendment, recorded in that file. For buffers longer than 19 s, the gate applies to the first 19 s. This adopts the clean-room's earlier pre-19 s demo-window policy. The original's unlicensed-demo interruptions begin at about 19.88 s and are not reproduced. Whole-buffer results are still reported.

- Calibrated captures: `ns1-research/analysis/characterization/capture` (NS1 Mono, fresh instance, no warmup). 42/42 established-control rows pass, covering silence, impulse, tone, white noise, tone+noise, noise step, level ladder, chirp, blocks 64/512/2048, 44.1/48/96 kHz, repeat/reset, a 180 s offline and a 95 s paced capture. Worst established-row errors (rows passing whole-buffer): highest nrmse 9.3e-7 (96 kHz white noise @ 100 %, max 2.8e-7); highest max |err| 2.1e-6 (96 kHz tone+noise @ 50 %, nrmse 2.6e-7). Many rows are bit-exact. The long rows are 0.0 error over the first 19 s and fail whole-buffer only because of the demo interruptions. Report: `fidelity-comparison.json`.
- Held-out speech (evaluation only, never tuned on): two fresh original renders, `6930-75918-0000-snr0` @ 50 % and `6930-75918-0006-snr10` @ 100 %. Results: nrmse 4.4e-8 / 1.2e-8, max 1.2e-7 / 1.4e-8. PASS. Report: `heldout-comparison.json`.

The restricted kernel/detector tests (`test_detector`, `test_mono`) are separate unit checks. They are not fidelity claims.

## Reproduce

```
cmake -S plugins/ns1 -B /tmp/ns1-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ns1-build -j2
ctest --test-dir /tmp/ns1-build --output-on-failure
PY=/home/felix/work/ns1-research/analysis/characterization/.venv/bin/python   # needs NumPy
$PY tests/ns1/compare_captures.py --capture <private capture dir> --render /tmp/ns1-build/ns1_render --report docs/evidence/ns1/fidelity-comparison.json
$PY tests/ns1/compare_heldout.py /tmp/ns1-build/ns1_render docs/evidence/ns1/heldout-comparison.json <oracle evidence dirs...>
```

The private captures, oracle evidence and analysis are not committed. No vendor binary, coefficient blob, asset or pseudocode is included.
