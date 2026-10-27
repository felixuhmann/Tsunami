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
host's render/state/rate/block/editor tests. Artifact checkout SHA and PR head SHA
are saved separately because pull_request checkout is a synthetic merge commit.
Full UI acceptance still requires human review of actual screenshots; the old
JUCE7 Windows screenshot attached an editor but was clipped, and is not JUCE9 proof.
Protocol validators, full UI proof on both platforms, package rebuild/download,
reviewed complete SBOM, hardware realtime and all reference DSP comparisons remain
separate gates. No auto-merge or public release.
