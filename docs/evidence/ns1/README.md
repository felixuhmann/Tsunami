# NS1 DSP checkpoint (partial, not a distributable denoiser)

Independent GPL-3.0-or-later C++17, no framework/UI dependency or vendor loading.

The adaptive band detector is implemented with explicit semantic coefficients and state. It includes the interband noise-floor ratio, asymmetric smoothing, fourth-power threshold followed by squared shaping, and the actual mixed float/double boundaries. A synthetic unit test was witnessed failing before implementation. A private samplewise comparison against the unmodified Linux kernel exercised all six bands at controls 0, 0.5 and 1, 10,000 samples each: gain, band signal and six observable state fields all matched exactly on the tested GCC/x86_64 build. This is a conditional detector result, **not** full-plugin equivalence or initialized-chain validation.

Build/test:

```
cmake -S plugins/ns1 -B /path/outside/source/ns1-build -DCMAKE_BUILD_TYPE=Release
cmake --build /path/outside/source/ns1-build -j2
ctest --test-dir /path/outside/source/ns1-build --output-on-failure
```

Private analysis, vendor artifacts, raw coefficient/state traces and oracle-only adapters remain outside this repository. They are not dependencies of this library. No measured coefficient blobs, original assets, pseudocode or original binary are committed. Subsequent mono-chain work and initialization/control-generation validation are in progress. Stereo is intentionally not implemented before mono fidelity passes. Full strict audio fidelity remains unestablished; no gate was weakened.
