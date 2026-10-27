# CLAP mono/stereo layouts (SDK note)

The shared `Adapter` accepts matched mono in/out only when `Spec::supportsMono` is `true`;
otherwise it stays stereo-only. VST3 mono tests do not prove anything about CLAP: the pinned
clap-juce-extensions wrapper (7adee3a1) exposes no `clap.audio-ports-config` extension and
reports only the bus default layout from `clap.audio-ports`.

## What the build does

`cmake/ClapAudioPortsConfig.cmake` takes the pristine upstream
`src/wrapper/clap-juce-wrapper.cpp`, checks its hash, applies the checked patch
`cmake/patches/clap-juce-wrapper-audio-ports-config.patch` and writes the result to
`<build>/tsunami-clap-wrapper/`. It then makes the upstream `clap_juce_sources` interface target
use that copy. `cmake/apply_source_patch.py` refuses fuzz or offsets, and the pristine input, the
patch and the output are all SHA-256-pinned (LF-normalized). The fetched dependency tree is not
written to, so the release source-integrity gates still apply. Rebuilding from the project source
archive plus the pristine dependency archive reproduces the same file.

The patch makes three changes:

- `clap.audio-ports` reports the bus's *current* layout. Until a host selects a config this is
  the default layout that upstream `init()` applies.
- `clap.audio-ports-config` is exposed only for a processor that has exactly one input bus and
  one output bus and accepts *both* matched mono and matched stereo. It then offers two configs
  with stable IDs, `1` = Mono and `2` = Stereo. Every other processor keeps the upstream
  behaviour, so the extension is absent and nothing is offered.
- `select` is rejected while the plugin is active, and for unknown IDs. Otherwise it applies the
  layout with `setBusesLayout`. The selected layout is kept across deactivate/activate.

The patch is not applied on macOS (the `.mm` wrapper includes the pristine file), and the build
warns about this. Configs are not stored in plugin state: hosts re-select them, as the CLAP spec
expects.

## Evidence

`tests/clap/clap_layout_test.cpp` is a thin ABI harness that loads the real compiled `.clap`
modules:

- **Mono-capable fixture:** a synthetic `MonoLayoutSpec` over the infrastructure fixture core.
  Not a target.
- **Ordinary fixture:** must stay stereo-only.

It checks config enumeration, invalid IDs, rejected selection while active, port readback (count,
type, in-place pairing), and processing of a known channel-distinct signal with sentinel guards at
1 and 2 channels. It also checks reselect/re-activate and a state round-trip.
`.github/workflows/clap-layouts.yml` runs it natively on Windows and Linux together with
clap-validator. A target plugin opting into mono still needs its own entry point that provides a
`Spec` with `supportsMono = true`. The shared `Plugin.cpp` instantiates the generated stereo-only
spec.
