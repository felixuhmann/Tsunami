# Plugin-local editor factory and bounded Linux capture

This is shared infrastructure, **not S1 UI or reference-fidelity completion**.
No target UI/assets or audio-thread meter processing are added.

## Optional editor contract

Place `Editor.hpp` beside the plugin's `parameters.json` and `Processor.hpp`.
It must include the JUCE declarations it uses and provide:

```cpp
namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(
    juce::AudioProcessor& processor,
    juce::AudioProcessorValueTreeState& state);
}
```

Implement the function in that header (or supply its linked definition). Return a
new editor bound to `processor`; ownership passes to JUCE/the caller. The editor
may retain references to the processor/APVTS, which outlive it. Create/destroy UI
on the message thread, use APVTS attachments for controls, and do not access the
portable core from the UI/audio threads concurrently. No public mutable APVTS
accessor is introduced. A null factory result remains null, not a fake fixture.

**Rerun CMake configure after adding/removing `Editor.hpp`.** The metadata CLI
checks only the metadata directory, emits its include and a `PluginSpec` factory
forwarder when present, and otherwise preserves the existing generic fixture.
The fixture fallback remains infrastructure-only, not an acceptable product UI.
Custom handwritten Specs may provide the same static `createEditor` signature.

Meters are deliberately deferred: the reference UI is not available, so there is
no established required meter model. This hook neither allocates nor adds locks
or work to `processBlock`.

## Linux race investigation and regression

Observed CI failure: [run 40502914100, Linux job](https://github.com/felixuhmann/Tsunami/actions/runs/40502914100/job/122184836771).
The downloaded log completed all GUI tests and `SUCCESS`, but the capture gate
reported no attached editor. Five unmodified local replays against the downloaded
Linux artifact passed; the original CI scheduling delay is not recoverable from
its logs, so its exact cause is not asserted.

Source investigation identified a reproducible lifecycle hazard in pinned
[pluginval 1.0.4 BasicTests.cpp](https://github.com/Tracktion/pluginval/blob/v1.0.4/Source/tests/BasicTests.cpp):
`EditorAutomationTest` holds its editor for only 100 iterations of 10ms at
strictness 5, versus 1000 iterations above 5. Local X11/log tracing observed its
window from approximately 0.438s to 1.449s. A deterministic native regression
injects 1.25s into the first X11 discovery during Editor Automation; the original
capture script then fails with the **same missing-capture assertion**, despite
validator SUCCESS.

The fix selects **strictness 6**, extending the actual editor lifetime to about
10 seconds through the host's supported test mode, and adding validator coverage.
It does not skip/retry away a failed validator, remove identity/module/liveness
checks, substitute a screenshot, or make any GUI test optional. The existing
60-second capture deadline/90-second CI envelope remain. This is a bounded
latency fix, not a guarantee under arbitrary host stalls.

## Witnessed validation

Private durable evidence root: `/home/felix/work/tsunami-campaign/editor-hook-logs/`.

- `hook-red.log`: compiled native test exits 9, factory not called.
- `hook-green.log`: same test exits 0; actual processor/APVTS identity, physical
  parameter processing, state restore, and fixture fallback pass under JUCE9.
- `metadata-red.log`: CLI fails selection assertion before generator change.
- `python-green.log`: all 12 foundation Python tests pass after generator change.
- `capture-red-0.log`: injected native discovery delay fails missing capture.
- `capture-green-0.log`: identical delayed native gate passes at strictness 6;
  `capture-green-0/` retains actual image, loaded-module identity, hashes and
  complete successful pluginval log. Image inspected: fixture title and Gain
  control; no blank frame/error dialog. This uses the downloaded CI fixture, not
  a claim of a new target module build.

Native regression command (needs Pillow, X11 tools and pinned pluginval):

```sh
xvfb-run -a -s '-screen 0 1600x1000x24' python \
  tests/foundation/capture_delay_regression.py "$plugin" "$output" "$pluginval"
```

Native adapter test was freshly compiled from this worktree and linked to cached
unchanged JUCE9 objects, without modifying the shared build cache. The private
runner records flags/output and serializes under heavy slot 1, checking available
memory first. Existing JUCE `Font(float,int)` deprecation warning remains; it is
not introduced by this change. Native Windows and CI exact-head checks remain
required; no S1 UI, meter behavior, reference screenshot or product acceptance is
claimed here.
