# Per-plugin acceptance contract

A plugin is done only when all gates below are evidenced for the exact PR head. Draft PRs, stub DSP, plausible screenshots, successful compilation, self-reported tests and Wine-only testing are not completion.

- Independently written DSP derived from actual reference analysis; no generic substitute falsely described as equivalent. Exact reference version/hash, supported component/control mapping and evidence provenance recorded.
- Real reference captures and candidate comparisons on calibrated probes and held-out representative material. State numeric error/fidelity thresholds and known differences before claiming equivalence. Unsupported modes remain explicit. Do not fit or remove licensing controls.
- Original interactive UI with working controls, parameter/state synchronization and real screenshots of the running built UI (not a browser mockup or invented image). Include screenshot paths/commit and native platform details.
- Native Windows x64 and Linux x64 build/test jobs pass. Exercise the actual plugin in a host on each OS: enumerate, parameter roundtrip, process nontrivial audio, bypass, state restore, rate/block/channel matrix, finite output, silence/tails, and automation.
- Real-time path performs no allocations, locks, disk/network I/O; validate/measure processing safety and overload/error behavior. Report measured/reported latency and tails.
- Reproducible versioned release workflow packages VST3/CLAP, checksums and third-party notices for both OSes. Uploaded artifacts are downloaded and smoke-tested. Do not claim signing/notarization unless configured and verified.
- Each PR contains scope summary, reference-comparison report, actual UI screenshots, native CI links and downloadable build artifacts. No Waves binaries/assets, recovered code, secrets or private source data committed or shipped. Recovered pseudocode may inform an independent rewrite but is not shipped verbatim.
- Independent parent verification and read-back of PR/checks/artifacts precedes completion. No auto-merge or production release publication without review.
