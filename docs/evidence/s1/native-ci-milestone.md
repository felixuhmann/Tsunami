# Actual S1 native CI milestone (partial)

`.github/workflows/s1.yml` builds **Tsunami Stereo**, not the infrastructure
fixture, on native Ubuntu 22.04 and Windows Server 2022/MSVC. It runs the portable
suite, builds VST3/CLAP, exercises the CLAP protocol validator, and uses Pedalboard
to compare twenty VST3 rate/block combinations against the portable renderer.
State restoration, a fresh instance, bypass, silence and zero reported latency
are mandatory assertions. This remains static/reset control coverage, not
reference automation fidelity or complete target acceptance.

A separate fresh-runner job downloads the same-run artifact, with no checkout or
build, repeats the twenty-case test, and compares module/renderer SHA256 with the
build receipt. Review artifacts include the exact project/dependency source and
notices. These are experimental CI artifacts, **not published releases, audited
complete Corresponding Source, or archive-based source rebuild proof**.

## Witnessed Windows regression

The first native run, head `51f40ca7baa1d55455a7f073c7d1110dbdd95e00`, passed all
three portable tests and all three path tests, then failed the new binary-stream
regression with empty renderer output:

- [Windows failing job](https://github.com/felixuhmann/Tsunami/actions/runs/40507381378/job/122200053399)
- Finite float32 bypass input deliberately contains CR/LF and the Windows text-mode
  EOF byte. Text-mode stdin treated it as EOF; text-mode stdout could also expand
  LF. The following fix selects `_O_BINARY` for both streams and fails explicitly
  if either mode change fails. No DSP coefficients or equations changed.
- The native host now resolves/hashes the exact Windows module inside its VST3
  bundle; Linux resolves its ELF module. Platform-path changes were separately
  RED/GREEN tested, including rejection of unsupported operating systems.

Latest native run/check URLs, artifact IDs, results and downloaded evidence must
be read back in PR #16. A workflow definition or successful Linux run does not
prove Windows passed.

## Still missing

No original-layout editor is implemented or claimed. S1's genuine reference
editor cannot currently initialize in the private Wine environment; diagnosis
found a shell-side null dereference and an absent WebView2 runtime. This is not
native-Windows reference evidence. UI/screenshots, automation comparisons,
complete license/source inventory, source-archive rebuild and release gates remain
open. The shared infrastructure editor hook is reviewed separately in PR #17.
