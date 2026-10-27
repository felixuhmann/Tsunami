# S1 Imager Stereo — partial DSP checkpoint

Independent portable C++17 implementation for the S1 Imager Stereo component only. Not a completed product and not yet verified. Shuffler and MS-Matrix components are excluded.

## Interrupted-worker provenance

The resumed worktree contained uncommitted matrix/control/smoothing implementation, processor assertions, renderer and comparison script. These were reviewed and preserved, not discarded. Their original RED-before-GREEN history is unavailable; this checkpoint does **not** claim strict TDD provenance for those inherited changes. New behavior changes require a witnessed regression failure before modification.

Recovered behavior includes width-dependent normalization, gain, rotation, asymmetry, input-column polarity/swap, M/S input decode and 40-ms coefficient smoothing. Physical gain is -24..0 dB; stereo only. Unsupported channel layouts are safety passthrough, not claimed reference variants. Explicit replacement bypass is not the vendor host bypass implementation.

## Reproduction

```
cmake -S plugins/s1 -B /absolute/private/build
cmake --build /absolute/private/build -j2
ctest --test-dir /absolute/private/build --output-on-failure
python3 tests/s1/compare_oracle.py /absolute/private/build/s1_render /absolute/private/oracle/capture [physical-controls...]
```

Private acquisition, decompilation, original audio and raw captures stay outside the public repository. Public evidence will contain only independently generated aggregate measurements and hashes, never vendor code/assets or raw reference output.

## Outstanding acceptance gates

Fresh calibrated and reserved held-out comparisons; full transition/automation and block/rate coverage; independently drawn original-layout working UI and genuine hosted screenshots; native Windows/Linux plugin verification and downloadable artifacts; exact-head independent parent review. The generic framework fixture is not a completed S1 UI.
