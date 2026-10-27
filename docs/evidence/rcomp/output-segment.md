# Physical output-control segment

This is newly implemented processing, **not a complete RComp Processor**.
`plugins/rcomp/OutputStage.h` maps physical Gain (dB), Trim (dB) and Mix (%) to
an actual aligned dry/wet audio output. No generic compressor or passthrough
Processor was added. The existing GainStage/GainHistory source is unchanged.

## Recovered behavior

With `g=10^(Gain/20)`, `t=10^(Trim/20)` and `m=Mix/100`, the static output is
`dry * float((1-m)*t) + (wet*g) * float(m*t)`.

Trim affects **both** paths; makeup Gain affects only the wet path. This matters:
applying Trim only to wet fails the genuine combined-control capture by RMS
0.011632842074842128. Setters reject nonfinite/out-of-range inputs atomically;
processing is allocation-/lock-/I/O-free and allows either input to alias output.
The caller supplies time-aligned dry and compressed wet; setters are single-owner,
not concurrently writable. This segment deliberately does not pretend to produce
its own still-incomplete compression signal or to implement automation smoothing.

Grounding: unmodified RComp 17.1.42.50 Windows module SHA256
`4ecaafa3628a11c3e34e60c34182d7daabefadd3f231918fd47bcefe88d5ac79`.
Private PE control analysis identified coefficient writer `0x18002e0d0`:
Mix/100 times dB-to-linear Trim is written to coefficient index29, its complement
times Trim to index30. The ELF mono loop consumes those at offsets `+0x74/+0x78`
and mixes aligned audio after wet gain. PE output-gain writer `0x18002cef0`
includes the internal fixed-point normalization; this standalone output segment
accepts a normalized wet signal, so it does not apply that normalization twice.
Recovered pseudocode and vendor material remain private and are not published.

## Genuine comparisons, not analytic-only characterization

Six fresh original captures were executed in the scoped nonroot/no-network
Wine9 original runtime under the mandatory oracle lock. The native Linux C++
`rcomp_output_stage_render` executable, not a Python copy of the proposed formula,
processed the genuine dry+wet baseline samples for comparison against four
separately rendered original control configurations:

| Controls changed | Samples | Maximum absolute error | RMS error |
|---|---:|---:|---:|
| Mix25 | 48000 | 7.450580596923828e-09 | 2.108329411276885e-09 |
| Gain+6 dB | 48000 | 1.4901161193847656e-08 | 3.2862938268752915e-09 |
| Trim-3 dB | 48000 | 3.725290298461914e-09 | 1.0576143956832215e-09 |
| Gain+6, Trim-3, Mix25 | 48000 | 1.4901161193847656e-08 | 3.7480321581279414e-09 |

**192000 compared samples, all four segment gates pass.** Thresholds: max
absolute error <=3e-6 and RMS <=5e-7.

*Threshold chronology qualification (independent review R3).* The private
threshold file was created ~88 ms before the first (wet) capture request, but it
was rewritten with the same values at the start of the second batch (after the
wet/mix25/gain6/trim-3 results, before dry/combined), and no immutable copy or
digest of the initial bytes, nor the initial launcher, was retained in any
capture receipt. The later batch's predeclaration is shown by its retained
launcher; for the first four captures only file birth time supports it, which
does not prove the initial contents. No evidence suggests the limits were changed
or tuned after measurement, and the captures were deliberately not rerun to hide
this history. Future captures must hash an immutable threshold manifest and
launcher into every receipt. A deliberately wrong
Trim placement is rejected by those same gates. Original Ratio request4 reads
back **4.01**, preserved rather than silently relabelled. All are 48kHz, mono,
block512, fresh/reset instances with static controls, Manual/Smooth compression.
No demo interruptions were removed and no held-out material was used.

A genuine dry capture established the PCM16 input sample domain (approximately
float32(PCM/32767), max calibration error7.45e-9). The initially assumed /32768
domain caused a mix RMS error1.22e-6; it was not hidden by relaxing thresholds.
Comparison uses the actual original dry capture, not a fitted correction.
[Machine-readable controls, hashes and measurements](output-segment-comparison.json).

**This is segment fidelity with genuine compressed wet input, not end-to-end
candidate compressor fidelity.** Gain-dependent limiting at high levels,
control-transition smoothing, stereo, rates/blocks beyond this matrix, native
Windows, a hosted candidate and full startup/latency remain unverified.

## Witnessed TDD and changed-scope verification

1. Gain/trim test failed because `OutputStage.h` was absent; physical dB path
   implemented; gain/trim and polarity passed.
2. Mix test failed because `setMix` was absent; recovered shared-trim placement
   implemented; wet/dry endpoints and combined controls passed.
3. Block test failed because `process` was absent; in-place processing implemented;
   exact sample/block agreement, zero-length processing and invalid-control
   rejection passed.

Commands actually exercised (GCC11.4/Linux):

```sh
cmake -S plugins/rcomp -B /private/output-build -DCMAKE_BUILD_TYPE=Release
cmake --build /private/output-build --target rcomp_output_stage_tests rcomp_output_stage_render -j1
ctest --test-dir /private/output-build -R '^rcomp_output_stage$' --output-on-failure
```

Result: **1/1 changed-scope CTest passed**. Direct ASan/UBSan/float-cast-overflow
build/run passed the same new test without findings. Existing unchanged320-case
kernel evidence was reused, not rerun. Private raw logs and comparison runner:
`tsunami-reference/analysis/rcomp/output-segment/` and `compare_output_segment.py`.

## Next boundary

PE initialization analysis additionally recovered real sample-rate-dependent
history geometry at `0x18002ccd0` and ARC generation at `0x18002d7d0` (private).
Those are now available for the next implementation, not claimed implemented by
this output segment. Integrate the detector/ARC/soft-gain path and actual history
prefill/normalization so OutputStage can consume an independently generated wet
signal. Do not register a complete Processor until that chain is credible.
