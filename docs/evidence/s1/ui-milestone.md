# S1 editor milestone: Tsunami Stereo working editor (PARTIAL)

Status: **partial / draft**. This milestone adds an original, independently drawn
working editor. It is **not** a claim of target completion or pixel equivalence.

## Visual reference used
Documentation-derived visual reference only (not an original-host capture):
the official product image (SHA256 62e4e4cb…f96d) and the official manual
(SHA256 4955022c…0d2970; printed p5 vector display, p6/7 controls, p22 key
commands), both kept privately outside the repository. Nothing was sampled,
embedded or traced; all drawing is JUCE code. The original binary's editor
still does not open under the private Wine setup (WebView2/WoW64), so no
same-version visual comparison exists.

## What was implemented
- `shared/Adapter.hpp`: optional `tsunami::MeterSource` interface. After the DSP in
  `processBlock`, block peaks of output L, R, M=(L+R)/2 and S=(L-R)/2 are merged
  into four `std::atomic<float>`s with a CAS max (no locks, no allocation).
  `takeOutputPeaks()` exchanges them to zero. The fixture editor and generic
  behaviour are unchanged.
- `plugins/s1/EditorModel.hpp`: framework-independent vector-display model. It runs on
  a **private** `Processor` instance, using a new read-only `Processor::matrix()`,
  so the display always comes from the same matrix as the DSP. The audio-thread
  core is never touched. Also holds the bounded drag mapping: plain drag X=rotation,
  Y=gain; Alt-drag X=asymmetry, Y=width.
- `plugins/s1/Editor.hpp` (wired in through the reviewed PR17 hook):
  - Three panels. Left: Gain/Width vertical faders with editable values, L-R|M-S
    input toggle, +/- (invert left), <-> (swap) and +/- (invert right).
  - Centre: a static control-transfer vector display (semicircle, 0…-24 dB scale,
    L/C/R transfer drawn from the matrix) above the Asymmetry and Rotation faders.
  - Right: output peak bars and values, with a display-only L-R|M-S meter toggle.
  - Bottom: "Tsunami Stereo" name strip and Bypass.
  - All 9 parameters are bound through APVTS attachments. Double-click resets a
    slider. Double-click on the display, or the `R` key, runs reset image
    (width/rotation/asymmetry only; gain and input settings are kept). Display
    drags are wrapped in host gestures.
- `tests/s1/editor_test.cpp`: native JUCE test of the real editor. It checks the
  component IDs, UI→parameter writes, automation→UI updates, the vector model
  against the matrix, reset-image semantics, drag bounds, meters driven by
  `processBlock`, and that the meter mode leaves audio unchanged.
- Root `CMakeLists.txt`: registers `s1.editor` when building the real S1 target.

## TDD records (private, outside source: /home/felix/work/tsunami-campaign/s1-ui-logs)
- `meter-red.log`: adapter_test with the new meter assertions fails to compile because `tsunami::MeterSource` does not exist.
- `meter-green.log`, `meter-green-final.log`: adapter_test PASS, including output meters.
- `editor-red.log`: editor_test fails to compile because `tsunami::s1::vectorModel`/`Editor` do not exist. It also exposed a test-side typo (`convertTo0to1` on a base parameter), which was fixed before green.
- `editor-green.log`, `editor-green-final.log`: editor_test PASS.
- `s1-editor-inprocess.png`: in-process `createComponentSnapshot` of the actual editor after a processed block (M/S meter mode). **Not a hosted capture.**

Commands: `python3 /home/felix/work/tsunami-campaign/s1-ui-adapter-build.py <label> [test.cpp -I<gen> -Iplugins/s1]`
(compiles against the cached JUCE 9.0.4 objects under heavy-2.lock and runs under xvfb-run).
Core: `cmake -S plugins/s1 -B …/s1-ui-build/core && cmake --build … -j2 && ctest` → 3/3 pass.

## Remaining (not done in this milestone)
- No pluginval/Xvfb **hosted** capture of a built actual-target module yet, and no
  Windows hosted screenshot. `.github/workflows/s1.yml` is not yet extended with a UI capture job.
- The root-CMake `s1.editor` registration has not been exercised by a full CMake
  configure/build in this milestone (only the equivalent direct compile above).
- Manual p22 key commands beyond reset-image (`R`) are not implemented. Faders use
  stock JUCE drawing, not custom silver thumbs. The meters have no peak-hold or clip
  indicator, and the display-scale/ballistic behaviour is not measured against the original.
- No pixel or geometry comparison with the original (its editor is unavailable on this host).
