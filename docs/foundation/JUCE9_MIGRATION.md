# JUCE 9 migration evidence

This is a dependent infrastructure migration, not completion of any of the 12 DSP targets.

## Source provenance

- Base: foundation PR #13 at `35c5a8c7dab62721198f41198ddc2e0d7db02693`.
- JUCE release tag `9.0.4` resolves through the authenticated official GitHub API to
  `3fb5cbc0340758596dac39f7c8de47d562b4896e`.
- Downloaded `https://codeload.github.com/juce-framework/JUCE/tar.gz/3fb5cbc0340758596dac39f7c8de47d562b4896e`.
- Actual archive SHA256: `2bb7095497a7bb7269dbdf841a7c6aa0939eaeb2a2bc9724ac4758d3ae3565a7`.
- Official release `LICENSE.md` explicitly offers AGPLv3. This project selects it;
  no commercial account, subscription or licensing modification is involved.
- Full AGPLv3 text retrieved from SPDX license-list-data (`AGPL-3.0-only.txt`),
  SHA256 `d8a6cc31abc16b6748c7a21f21611f5a1ec33f67d22ca23d7da1c19b95496bee`.
- CLAP extension `git ls-remote HEAD` and official API main HEAD both resolve to
  the existing pin `7adee3a1bd4684d4caa5601100e364abccff4b4d`. Its newest tag
  is 0.26.0 at `cbe0fe959e784e27dd148805cd860fae6401da5d`.
  Keep newest upstream main, including its GUI resize guard. No newer revision
  was found. The upstream README only claims JUCE6/7/8; runtime evidence is needed.

## Strict TDD

1. Release/manifest/hash/license regression failed on actual JUCE7 version,
   then passed after the actual source pin and manifest update.
2. Local override regression failed because the CMake guard was missing, then
   passed: CMake rejects 7.0.13 before loading/building it and accepts 9.0.4.
   This is a version guard, not cryptographic attestation of arbitrary local edits.
3. Linux runner prerequisite regression failed because JUCE9 fontconfig headers
   were not declared, then passed after adding `libfontconfig1-dev`. The fetched
   foundation base already includes `x11-utils` for its real xprop readiness probe;
   the regression keeps that prerequisite protected.

Raw RED/GREEN logs are retained privately at
`/home/felix/work/tsunami-campaign/juce9-logs/01` through `06` (descriptive suffixes).
Run `python -m unittest discover -s tests/foundation -v` to reproduce config checks.

## Actual native execution / known limitations

Local build uses an isolated `juce9-build`, two compiler jobs and a campaign heavy
lock. It does not mutate the foundation branch, worktree or build directory.
First configure failed on missing fontconfig headers. After isolated extraction
of fontconfig dev packages, the second configure exposed missing uuid pkg-config
metadata, which suppressed fontconfig/FreeType link flags. Installed uuid/expat
headers into the same private sysroot; no global package/config changes.
All failed logs are retained, not replaced by successful runs.

Native CI builds actual VST3/CLAP and runs CTest plus the mature Pedalboard VST3
host's render/state/rate/block tests. Windows retains Pedalboard editor capture.
Linux uses pinned pluginval 1.0.4 for its real editor tests and capture (see below).
Artifact checkout SHA and PR head SHA
are saved separately because pull_request checkout is a synthetic merge commit.
Full UI acceptance still requires human review of actual screenshots; the old
JUCE7 Windows screenshot attached an editor but was clipped, and is not JUCE9 proof.
Protocol validators, full UI proof on both platforms, package rebuild/download,
reviewed complete SBOM, hardware realtime and all reference DSP comparisons remain
separate gates. No auto-merge or public release.

## Linux editor CI repair: a different mature host

The exact failed run is <https://github.com/felixuhmann/Tsunami/actions/runs/40492704827/job/122150354697>.
All 15 Pedalboard audio/state/rate/block cases passed, followed by X11 `BadWindow`,
`X_GetProperty`, resource `0x0` in `show_editor()`. The EWMH readiness probe had
already succeeded. This change does not assert a fix inside Pedalboard or JUCE.
It removes that failing host GUI path only on Linux, not the GUI acceptance gate.

- Pedalboard 0.9.26 still executes the unchanged audio/state matrix.
- Separate host pluginval 1.0.4 (official Linux release archive SHA256
  `c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352`)
  runs strictness 5 **without skipping GUI tests**.
- The capture helper is display/assertion orchestration, not a custom plugin host.
  It requires completed Editor, Open editor whilst processing, Editor Automation,
  exact plugin name and overall SUCCESS. It captures during Editor Automation.
- X11 window class and `_NET_WM_PID` must identify the launched in-process host;
  `/proc/<pid>/maps` must contain the exact requested native module. The window
  must remain viewable with unchanged identity before/after screen capture.
- Full screen and actual window crop, window tree/properties, host/module/image
  hashes, validator console log and source checkout SHA are retained. Capture
  uses composited screen pixels, not the unpainted host-parent backing store.
  An initially mapped but unpainted window is polled until actual pixels appear;
  no captured live editor means failure even when validator succeeds.
- Tests first failed on the old `--capture` Linux path and on missing identity
  validation, then passed. Negative log tests reject SUCCESS without GUI tests,
  and identity tests reject unrelated process/module paths.

Local execution used the **downloaded native module from the failed run**, not
another build: artifact checkout `6a63589cb39c10a82ae92bc2c347fdd7fc8fbf31`, PR
head `208ba3749392676b546ddbe2515cd988f31db60a`; module SHA256
`a84162affdaa10e3e13a710adabcee8e622148b4d40afc3aee286bf34a0e8a55`.
Evidence: `/home/felix/work/tsunami-campaign/juce9-logs/linux-editor-repair/`;
`editor-receipt.json`, `pluginval-editor.log`, `host-editor.png`,
`host-desktop.png`, `host-audio.json`, `host-receipt.json` and window identity files.
Actual screenshot reviewed: full title, Gain label/slider/value visible, no
splash/overlay/clipping. This remains an infrastructure fixture, not a target UI
or reference-fidelity result. Native CI at the new PR head is a separate gate.
