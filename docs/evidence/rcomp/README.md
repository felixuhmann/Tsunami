# RComp: partial coefficient-domain DSP milestone

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

Actual GCC 11.4.0/Linux result: **1/1 CTest passed**, warnings treated as errors.
The same executable passed with AddressSanitizer and UndefinedBehaviorSanitizer;
see [native-tests.txt](native-tests.txt). This proves the partial stage tests,
not a hosted VST3/CLAP or native Windows build.

## Exact remaining blockers / next bounded slice

1. Recover PE wrapper physical control → coefficient generation, lookup-table
   contents/initialization and sample-rate dependence. Do not infer milliseconds
   from a generic exponential envelope or reinterpret normalized ratio as linear.
2. Recover/integrate the kernel's maximum-history tree and ARC gain-increase table
   update, soft gain scale/offset, fixed-point integer gain smoother, audio delay,
   wet/dry coefficient smoothing, output gain/trim and metering.
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
