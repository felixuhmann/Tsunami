# RComp: partial detector, history and physical output-control processing

**Round-2 status (task 3).** `MonoProcessor.h` connects the wet chain (detector ->
gain law -> soft gain/ceiling -> history -> makeup) and the physical output mix.
Characterized from targeted genuine renders ([characterization-r2t3.json](characterization-r2t3.json)):
the static curve `gain=(1-b)*min(1,q+1.5q^2(1-q))+b`, `q=1/(1+m/10^(thr/20))`, a
measured threshold-independent Ratio floor table `b` (interpolation provisional),
and Attack feeding the recovered memory law. Gain also sets the c790 ceiling.
The detector uses a 64-sample sliding maximum (approximation of the reference tree).
Release uses a **provisional** one-pole (ARC d7d0 tables unrecovered).
Held-out comparison under unchanged [thresholds.json](thresholds.json): Mix 0 PASS
(max 7.5e-9); Mix 100 wet **FAIL** (max 6.3e-4, RMS 2.1e-5, rel RMS -63.7 dB;
previously max 0.035). Native tests: 8/8 ctest targets.

New milestone: [`OutputStage.h`](../../../plugins/rcomp/OutputStage.h) now maps
physical Gain/Trim/Mix to aligned dry/wet processing. Native C++ comparisons
against four fresh genuine original control renders pass over 192000 samples
(max absolute error1.49e-8); these use genuine wet+dry baselines and **are not
complete compressor fidelity**. [Scope, TDD and measurements](output-segment.md).
The existing detector/history stage remains unchanged. Static output control
mapping is recovered; automation smoothing and the independent full wet chain
remain incomplete.

Newest slice: [`HistoryGeometry.h`](../../../plugins/rcomp/HistoryGeometry.h) adds the
reference sample-rate bands (H 64/128/256 at 64/128 kHz edges), rectangle spans,
shift/normalization, nonzero startup prefill layout and makeup mantissa/exponent
split as a **partial state API, not yet integrated** with GainHistory or output.
[Scope, limits and TDD](history-geometry.md).

**Not a complete plugin, not reference-equivalent, and not adapter-ready.**
Related issue: <https://github.com/felixuhmann/Tsunami/issues/6>.

This checkpoint contains independently written C++17 DSP informed by analysis of
the actual RComp kernel. It is not a generic compressor offered as a replacement.
No vendor code, decompiler text, binary, resource, or raw reference audio is included.
Original source is GPL-3.0-or-later.

## Implemented scope

`plugins/rcomp/GainStage.h` implements a restricted internal coefficient-domain path:

- Three parallel first-order phase branches and maximum absolute detector, with the
  kernel's first-coefficient-zero branch-disable behavior.
- The recovered rational/cubic gain law: with positive scale `t`, nonnegative slope
  `s`, magnitude `m`, and shape `k`, define `q=t/(m*s+t)` and
  `g=min(1, q+k*q*q*(1-q))`. These are **internal coefficients**, not dB or ratio.
- Asymmetric gain following with caller-provided increase/decrease coefficients.
- A separate immediate-attack, exponential-release peak ceiling follower, selecting
  the lower gain. This subset uses unity soft-gain scale and zero soft-gain offset.
- Explicit reset and atomic rejection of invalid coefficient updates.

The stage returns a control gain, **not processed audio**. Its configuration is
single-thread-owner, like processing; it is not concurrently writable. Processing
uses fixed-size state without allocation, I/O, or locks. Finite audio inputs and
validated coefficients are required; full-plugin overload/nonfinite handling,
real-time timing/allocation instrumentation and denormal policy remain unverified.
The public static gain-law function requires finite nonnegative magnitude and the
same validated coefficient domain. Reset-to-unity/zero-history is an independently
chosen stage initialization, not yet proven to match the wrapper's initialization.

`plugins/rcomp/GainHistory.h` now adds an independently written **fixed-point
history → delayed wet-audio slice**, beyond the earlier control-gain helper:
float32-to-integer quantization, signed right shifts, modulo-32-bit second-order
smoothing, and aligned audio delay. Its restricted ring reduction and synthetic
internal geometry are explicit; physical-control mapping is still unknown.
[Detailed recovery, TDD and limitations](history-slice.md).

An actual differential execution against the unmodified original auxiliary ELF
passed **320 synthetic-coefficient cases / 327,680 samples with zero mismatches
and maximum absolute error 0**. This is **not a full-plugin physical-control
comparison or equivalence claim**. The separately written direct-FIR unit-test
path also passed 327,680 exact sample comparisons. Sanitized provenance and
results: [history-kernel-comparison.json](history-kernel-comparison.json).

`parameters.json` inventories 11 observed user controls, excludes generic MIDI
controls, and explicitly marks metadata-only status. Its indices are proposed
stable replacement indices, not VST3/vendor parameter IDs. Logarithmic UI hints
are replacement presentation choices, not recovered normalized mappings.

## Source grounding and boundary

Inspected the auxiliary **ELF before the PE wrapper**. Reference kernel:

- RComp 17.1.42.50, build 293546; distributed framework 17.1.42.54.
- `Linux64/XLMC`, 27,782 bytes, SHA256
  `95a060dc2868f8858e3102c8e9970d53b0ea4a0b9a2e1dc22ce04a48815a3822`.
- ELF link-time functions: `ProcessFuncM` at `0x2d70`, `ProcessFuncS` at
  `0x1100`, local `Output` at `0x2bc0`.
- REA 6.8.0/Ghidra 12.1.4 batch export; Ghidra analysis addresses add `0x100000`
  to these ELF addresses. No recovered pseudocode is published.
- Mono control record offsets used to identify the mathematics: phase coefficients
  `+0x04/+0x08/+0x0c`, peak ceiling/release `+0x10/+0x14`, soft-law scale/slope
  `+0x1c/+0x20`, shape-minus-one `+0x24`, gain-decrease memory `+0x28`.
  Gain-increase memory is runtime state updated from tables, **not a recovered
  fixed physical release mapping**. The implementation deliberately accepts that
  coefficient from the caller instead of inventing ARC behavior.

Private provenance/export stays under `tsunami-reference/analysis/rcomp/`:
`identity.json`, `static/XLMC.disasm.txt`, `decompile-result.json`, and
`decompiled/`. Analysis ran under the campaign heavy-job lock with a 512-MiB
Ghidra heap, CPU affinity 0,1 and niceness 10, after a memory-availability check.

## Actual oracle observations

[oracle-characterization.json](oracle-characterization.json) contains sanitized
numeric observations and hashes from three genuine captures, independently checked
against each retained NPY's hash, shape and finite samples. Raw captures remain
private in `tsunami-reference/runtime/evidence/rcomp/<capture>/`.

| Capture | Probe and controls | Observation |
|---|---|---|
| `20261027T112721-bd530122339e4866ba754206991d8bda` | 2-second impulse/stepped 1-kHz sine; threshold 0 dB, ratio 1, gain/trim 0, mix 100 | Input/output difference RMS 0; output RMS 0.1138915224 |
| `20261027T112846-f2f2b98afff7418e944e0c4fd7ad5386` | Same probe; threshold -24 dB, requested ratio 4, attack 0.5 ms, release 160 ms, Manual/Smooth | Actual ratio readback **4.01**; output RMS 0.03059433093; difference from input RMS 0.08642752261 |
| `20261027T113217-affc0519e2b942849b5eb9e22d462331` | Nine ascending DC levels, 0.5 seconds each; same compression, release 5 ms | Finite nonzero output; final 100-ms means preserved in JSON |

All captures: `RCompressor Mono`, 48 kHz, block 512, fresh instance, reset, no
warmup, unmodified original hosted by Windows CPython 3.11.9/Pedalboard 0.9.26
under Wine 9. This is **not native Windows evidence**. No demo interruption was
removed; short probes do not characterize demo timing. No held-out speech/noise
was used. Default transparency does not establish latency in other modes.

These demonstrate a functioning, control-responsive oracle, **not candidate
fidelity**. End-to-end candidate error is **not measured**: converting physical
controls to coefficients and downstream processing is still unrecovered. No
fidelity threshold is met or asserted. Unit-test tolerance is `1e-7` on analytic
coefficient-domain vectors, not an audio equivalence threshold.

Oracle invocation (private runner serializes its own mandatory oracle lock):

```sh
python3 /home/felix/work/tsunami-reference/runtime/oracle.py render \
  --target rcomp --component 'RCompressor Mono' --input /absolute/probe.wav \
  --rate 48000 --block 512 \
  --params '{"threshold":{"value":-24.0},"ratio":{"value":4.0},"attack":{"value":0.5},"release":{"value":160.0},"arc_manual":{"raw":1.0},"warm_smooth":{"raw":1.0}}'
```

## TDD and native Linux verification

Each implementation slice followed RED → GREEN before the next slice:

1. Static law: compilation failed because `GainStage.h` did not exist, then the
   analytic silence/knee/above-knee/zero-slope/clamp vectors passed.
2. Streaming detector/follower: compilation failed on absent `Coefficients`,
   `configure`, `tick` and `reset`, then the impulse/reset vectors passed.
3. Atomic invalid-update rejection: executable failed with
   `accepted zero denominator scale`, then validation was implemented and passed.
4. Parallel ceiling: compilation failed on absent `ceiling`/`ceilingRelease`, then
   immediate attack/release/reset passed.
5. Added regression tests for polarity symmetry, gain bounds over 20,000 samples,
   and first-coefficient-zero detector disabling; no new DSP added for these.

```sh
cmake -S plugins/rcomp -B /path/outside/source/rcomp-build -DCMAKE_BUILD_TYPE=Release
cmake --build /path/outside/source/rcomp-build -j2
ctest --test-dir /path/outside/source/rcomp-build --output-on-failure
```

Actual GCC 11.4.0/Linux result at this checkpoint: **2/2 CTests passed**, warnings
treated as errors. Debug AddressSanitizer + UndefinedBehaviorSanitizer +
float-cast-overflow also passed **2/2 CTests**; see
[history-native-tests.txt](history-native-tests.txt). The earlier helper-only
checkpoint is retained in [native-tests.txt](native-tests.txt). This proves partial
standalone DSP tests, not a hosted VST3/CLAP or native Windows build.

## Exact remaining blockers / next bounded slice

1. Connect `HistoryGeometry` (implemented, partial) to the wet chain: resolve the
   prefill/mantissa writer storage format and buffer length, map the nonzero
   prefill onto the GainHistory ring/delay, and integrate ARC generation
   (`0x18002d7d0`) and state init (`0x180028450`). Coefficient packing at
   `0x18002e4f0` remains unresolved. Exports stay private.
2. Recover/integrate the kernel's maximum-history tree and ARC gain-increase table
   update and soft gain scale/offset. Static wet/dry Mix, output Gain and Trim now
   exist in OutputStage; automation smoothing, overload interaction and metering
   remain. Integrate the fixed-point smoother/audio delay with actual wrapper
   geometry and startup/prefill; restricted history evidence does not establish
   the complete path.
3. Cover stereo linking, Opto/Electro, Warm/Smooth and expansion ratios below one.
4. Only then implement the complete `tsunami::rcomp::Processor` portable contract
   (`prepare/reset/setParameter/getParameter/process/latencySamples/tailSeconds`).
   **It intentionally does not exist yet**; there is no passthrough placeholder
   and no root-build/framework registration of this partial stage.
5. Establish independent end-to-end candidate/oracle error thresholds and a rate,
   block, channel, state and automation matrix before equivalence claims.
6. Original interactive UI, actual hosted screenshots, native Windows/Linux plugin
   CI, downloadable artifacts and release/source/license gates remain absent.

Keep the PR draft and issue open. No completion/verified status is appropriate.
