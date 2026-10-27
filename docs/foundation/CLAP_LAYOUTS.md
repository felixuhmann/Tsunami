# CLAP mono/stereo layouts (SDK note)

The shared `Adapter` accepts matched mono in/out only when `Spec::supportsMono` is `true`;
otherwise it stays stereo-only. VST3 mono tests do not prove anything about CLAP: the pinned
clap-juce-extensions wrapper (7adee3a1) exposes no `clap.audio-ports-config` extension and
reports only the bus default layout from `clap.audio-ports`.

## What the build does

`cmake/ClapWrapperDerivation.cmake` runs `cmake/derive_source.py` on the pristine upstream
`src/wrapper/clap-juce-wrapper.cpp` and writes a derived copy to
`<build>/tsunami-clap-wrapper/`, then points the upstream `clap_juce_sources` interface target at
that copy. The helper requires the exact pinned input SHA-256 (after normalizing a CRLF checkout to
LF, which it labels), requires every anchor in `cmake/clap-wrapper-audio-ports-config.json` to
match exactly once, and refuses to write unless the result matches the pinned output SHA-256. An
identical existing output is not rewritten. The fetched dependency tree is never written to, so
the release source-integrity gates still apply; the pristine dependency archive plus the project
source reproduces the same derived file. `tests/clap/derive_source_test.py` exercises these
rejections on real temporary files (CTest `clap.layouts.derive_source`).

The edits make four changes:

- `clap.audio-ports` reports the bus's *current* layout for processors that offer configs (before
  any selection this is the default layout applied by upstream `init()`). Other processors keep
  reporting the default layout.
- `port_type` is set to `nullptr` before the mono/stereo checks, so a bus that is neither never
  returns an uninitialized pointer.
- `clap.audio-ports-config` is exposed only for a processor that has exactly one input bus and
  one output bus and accepts *both* matched mono and matched stereo. It then offers two configs
  with stable IDs, `2` = Stereo (index 0, the default) and `1` = Mono (index 1). Every other
  processor keeps the upstream behaviour: the extension is absent.
  The gate relies on the processor's `isBusesLayoutSupported`. Tsunami processors all go through
  `Adapter`, which accepts mono only with `Spec::supportsMono`. A raw JUCE processor that keeps
  JUCE's permissive default would be offered both configs, so new entry points must override it.
  "Index 0 is the default" assumes a stereo default bus, which is what `Adapter` declares.
- `select` is rejected while the plugin is active and for unknown IDs; otherwise it applies the
  layout with `setBusesLayout`. The selected layout is kept across deactivate/activate.

Known limitations:

- The derivation is skipped on macOS (the `.mm` wrapper includes the pristine file), so macOS
  CLAP builds offer no configs.
- `<build>/tsunami-clap-wrapper/` is regenerated at configure time only; a deleted or hand-edited
  copy is repaired by the next configure (CI re-hashes it).
- The selected config is not stored in plugin state; after a reload the host has to select it
  again.
- `clap.audio-ports-config-info` (current-config readback) and `configurable-audio-ports` are not
  implemented. The current layout can be read back through `clap.audio-ports`.
- At the default checking level, clap-helpers rejects `select` while active before the derived
  wrapper's own guard runs.

## Evidence

`tests/clap/clap_layout_test.cpp` is a thin ABI harness that loads the real compiled `.clap`
modules:

- **Mono-capable fixture:** a synthetic `MonoLayoutSpec` over the infrastructure fixture core.
  Not a target.
- **Ordinary fixture:** must stay stereo-only.

It checks config enumeration, invalid IDs, rejected selection while active, port readback (count,
type, in-place pairing), and processing of a known channel-distinct signal with sentinel guards at
1 and 2 channels. It also checks reselect/re-activate and a state round-trip.
The harness, its mono fixture and `clap.layouts.derive_source` are registered only with
`-DTSUNAMI_BUILD_CLAP_LAYOUT_TESTS=ON` (default OFF), so the canonical fixture build registers only
tests whose targets it builds. `.github/workflows/clap-layouts.yml` enables the option, asserts that
exactly these three tests are registered and runs them natively on Windows and Linux together with
clap-validator. A target plugin opting into mono still needs its own entry point that provides a
`Spec` with `supportsMono = true`. The shared `Plugin.cpp` instantiates the generated stereo-only
spec.
