# Restricted fixed-point gain-history / delayed-audio slice

**Partial internal DSP, not a complete RComp replacement.** The new
`plugins/rcomp/GainHistory.h` independently implements the downstream gain-history
and delayed-audio path. It does not guess physical attack/release/ratio mappings.

## Recovered behavior and restricted geometry

Source: unmodified RComp 17.1.42.50 auxiliary ELF `Linux64/XLMC`, SHA256
`95a060dc2868f8858e3102c8e9970d53b0ea4a0b9a2e1dc22ce04a48815a3822`.
The mono float-input kernel is `ProcessFuncM`, ELF link-time VA `0x2d70`.
Instruction-level checks at `0x3825–0x38df` disambiguated floating conversion,
signed shifts, modulo integer operations, and the final double-to-float scaling.
No vendor source, disassembly, or decompiler text is reproduced here.

The original kernel shares storage between detector history, integer gain history,
and delayed audio. For the explicitly restricted geometry

- `A = firstSpan` (control record `+0x60`), strictly positive;
- `B = middleSpan` (`+0x64`), nonnegative;
- `H = audioDelay` (`+0x6c`), a power of two;
- total original shared-ring length `R = 2*H + 2*A + B` (`+0x58`),

its complete index traversal moves backwards one slot per sample. The gain taps
therefore represent current input and delays `A`, `A+B`, `2*A+B`, while the audio
read represents delay `H`. The independent implementation uses separate typed
circular buffers rather than copying the original mixed-type storage layout.
**The wrapper's actual geometry selection and physical-control mapping remain
unknown.** This algebraic restriction is not asserted to cover every product mode.

For each incoming internal gain `g`, convert the float32 product `g * 2^31` by
truncation toward zero, reproducing x86 `CVTTSS2SI`. Out-of-range and nonfinite
products produce its `INT32_MIN` sentinel. In particular, **unity is not saturated
to INT32_MAX**. Apply signed arithmetic right shift `s` before history summation.
With shifted integer history `x` and zero initial histories:

`y[n] = x[n] - x[n-A] - x[n-A-B] + x[n-2*A-B] + 2*y[n-1] - y[n-2]`

All recurrence operations wrap modulo `2^32`, not saturate. The output gain is the
signed accumulator converted through double precision with the recovered scalar
`4.656612873077e-10`, then rounded to float32. Wet audio output multiplies that
gain by the `H`-sample-delayed input, with unity external output scale and wet
weight, and zero dry weight. Other gain scale/mix choices are not implemented.

The independent test path uses a **direct finite convolution**, not this recursive
formula: rectangular windows of lengths `A` and `A+B`. This checks the recurrence,
tap indexing, signed shifts, wrapping and delayed-audio alignment independently.

## TDD and actual native execution

Sequential RED → GREEN observations before each production change:

1. Missing `GainHistory.h` caused the expected compilation failure. Implemented
   the analytic trapezoidal step and delayed-audio slice; both local tests passed.
2. Missing `reset` caused compilation failure. Added clearing of both histories
   and recursive state; tests passed.
3. Test failed with `accepted zero first span`. Added bounded geometry validation,
   retaining prior configuration/state on invalid requests; tests passed.
4. The unity endpoint test under `-fsanitize=undefined,float-cast-overflow
   -fno-sanitize-recover=all` failed with `2.14748e+09 is outside the range of
   representable values of type 'int'`. Added defined x86 conversion semantics;
   endpoint/nonfinite/signed-shift tests and sanitizer execution passed.
5. Test failed with `accepted invalid history geometry` for a non-power-of-two
   delay. Restricted the supported geometry; tests passed.

The final public direct-FIR regression exercised **320 geometries and 327,680 exact
sample comparisons**, including negative words, unity sentinel, overflow/wrap,
shift 0/31, zero middle span and repeated history wrapping. GCC 11.4.0 native
Linux Release **2/2 CTests passed**. Debug AddressSanitizer + UndefinedBehaviorSanitizer
+ float-cast-overflow **2/2 CTests passed**. Actual commands/output:
[history-native-tests.txt](history-native-tests.txt).

`prepare` allocates off the audio thread and imposes replacement safety limits:
`2*A+B <= 65536`, `H <= 65536`, and shift in `[0,31]`. These limits are not recovered
vendor parameter ranges. Call `prepare` before `tick`; use a single owner and finite
audio. `tick` has no allocation, lock or I/O by inspection; allocation/timing
instrumentation has not been performed. Reset uses zero initial histories; the
full wrapper's startup/prefill protocol is not recovered.

## Actual original-kernel differential execution

A private harness called the **unmodified original ELF** and the independent
candidate, using synthetic internal coefficients. The original's earlier gain
computer was held at unity and its downstream soft-gain multiplier supplied the
same deterministic gain sequence to the recovered history stage. Synthetic cases
used `A={1,2,3,7}`, `B={0,1,2,5}`, `H={1,2,4,8}`, `shift={0,1,3,7,31}`; 1,024 samples
per case, deterministic changing audio/gain, with periodic gain zero and unity.

Actual result: **320 cases, 327,680 samples, 0 mismatches, maximum absolute sample
error 0** in that restricted experiment. See
[history-kernel-comparison.json](history-kernel-comparison.json) for source,
reference, immutable harness/snapshot hashes and exact scope. This is stronger
than analytic tests but **is not a physical-control full-plugin comparison**, a
held-out fidelity result, a native Windows result, or proof of all product modes.

Execution used the common mandatory oracle flock and heavy-2 lock. The immutable
per-run snapshots were mounted read-only in a nonroot, network-disabled,
resource-limited container, pinned by image ID; label-scoped orphan inspection
and cleanup surrounded the run. No shared Wine prefix was mounted or modified.
No binary was patched and no licensing control was altered.

Private reproducer and retained evidence:
`tsunami-reference/analysis/rcomp/history_kernel_probe.cpp`,
`run_history_kernel_probe.py`, and
`history-kernel-evidence/20261027T121703435311Z/`.

## What remains

Physical controls and sample rate → coefficients, actual wrapper ring geometry,
ARC table generation/history, maximum detector tree integration, soft-gain
scale/offset, mix/output smoothing, stereo/modes/expansion, startup state and full
`Processor` remain unfinished. So do original working UI/screenshots, native
Windows/Linux hosted plugins, end-to-end acceptance thresholds and release gates.
Keep PR15 draft and target status partial/unverified.
