# S1 editor milestone: Tsunami Stereo working editor (PARTIAL)

> **CURRENT STATUS (supersedes older "no plugin-local hook / no product UI / no native CI" statements in the history below)**
> - A plugin-local editor (`plugins/s1/Editor.hpp`) is wired through the merged foundation editor hook. It is an
>   independently drawn, docs-derived original-style UI, not the original-host appearance. It is covered by native
>   editor and RT tests in `.github/workflows/s1.yml`.
> - Native CI exists for the actual target on Windows and Linux: DSP 20-case comparison, downloaded-artifact smoke
>   and hosted editor capture.
>   - Only head `9862ef0` has all four jobs verified by the parent (run 40513113985).
>   - Run 40516362962 for `65c9f23` was cancelled by a later push.
>   - Run 40517143898 for `76ca0ce` was in progress when this was written.
>   - Later local commits are pending native CI.
> - **Update 2026-10-27 (supersedes "original GUI renders black").** A genuine original S1 Imager 17.1.42.50 editor
>   has now been captured statically and painted, privately. Private evidence:
>   - `original-viewability-20261027T135616/child-12-10485772.png` (sha256 e5dc30a9…5d69);
>   - report `original-editor-bounded-20261027T1413.json` (sha256 b00cce99…b9a).
>   It was hosted under Wine with a demo banner, and nothing from it is shipped. It is **static visual evidence
>   only**: original drags did not change its readouts, so original interaction and automation parity remain
>   unverified. Calibrated against it so far: the shared Input/Meter Mode selector row, and Width 1.00 at mid-travel.
> - Target remains a **draft/partial**. Open items:
>   - exact linked-drag sensitivity, vector projection and meter ballistics;
>   - rectangular multi-control selection and editor resize;
>   - original interaction/automation comparison (static reference only);
>   - release, SBOM and source-archive gates.
>
> Detailed UI receipts: `ui-milestone.md`.

Status: **partial / draft**. This milestone adds an original, independently drawn
working editor. It is **not** a claim of target completion or pixel equivalence.

## Visual reference used
Documentation-derived visual reference only (not an original-host capture):
the official product image (SHA256 62e4e4cb…f96d) and the official manual
(SHA256 4955022c…0d2970; printed p5 vector display, p6/7 controls, p22 key
commands), both kept privately outside the repository. Nothing was sampled,
embedded or traced; all drawing is JUCE code. The original binary's editor
was blocked under private Wine (WebView2/WoW64). The parent has since installed the
official offline WebView2 runtime in a fresh isolated WoW64 prefix, but no genuine
editor capture exists yet, so no same-version visual comparison exists.

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

## Correction slice 2 (after parent review)
Each fix had its own witnessed red/green run (logs in `s1-ui-logs`). The original
`editor_test` red was a single compile failure. It is **not** a red witness for each
behaviour that test covers.
1. **RT regression.** `tests/s1/editor_rt_test.cpp` notifies all parameters from a
   worker thread after one warm-up notification, with the editor open and no message
   pumping. It counts operator-new calls on that thread. `rt-red.log`: FAIL, 175
   allocations (`callAsync` + `SafePointer` in the editor's APVTS listener).
   Fix: the listener is removed. The message-thread timer compares raw parameter
   values and repaints. `rt-green.log`: 0 allocations. This does not claim every JUCE
   host-automation path is allocation-free.
2. **Vector angle regression.** At unity, L/C/R must sit at -45/0/+45 degrees, and
   rotation 20 must put the centre at 20 degrees (ordinary M/S coordinates).
   `vector-red.log`: FAIL code 8 (the old doubled angle gave ±90). Fix: angle =
   atan2(R-L, R+L). The drawing is now an origin-L-R triangle plus an independent
   centre ray. `vector-green.log` PASS. The exact original radial/angle projection
   is still **pending**: it has not been established beyond the manual diagram.
3. The +/- symbols now follow polarity. The input and meter switches show
   "L-R active"/"M-S active". There was no separate red for these.
4. The meter bridge has `static_assert(std::atomic<float>::is_always_lock_free)`.
   The CAS max is lock-free, **not wait-free**.
7. Body geometry: see `geometry-red/green` above.
5. Candidate in-process renders: `s1-ui-logs/candidates/candidate-{default,width2,rotation20}.png`.
6. CI (`s1.yml`) now builds `s1_editor_test` and `s1_editor_rt_test` for ctest. It
   also adds actual-module UI capture: on Linux, `tests/s1/linux_editor_capture.py`
   (the foundation pluginval strictness-6 capture with the S1 title); on Windows,
   `tests/foundation/host_smoke.py --capture`. Screenshots, receipts and hashes go to
   `evidence/ui`. The existing native DSP comparison and downloaded smoke are unchanged.

### Still pending
- (Done in slice 7.) Fixed 1048x665 body with 222-wide side panels and the display at
  244/32/560x321. `geometry-red.log`: FAIL code 18. `geometry-green.log`: PASS.
  Fixed size only; there is no scaling.
- Grooved silver fader caps, tick scales, held-peak/clip lamps and Alt-click reset
  are not implemented.
- There is no test of state restore while the editor is open. Full state/UI
  acceptance is pending.
- The root full CMake build has not been run locally. CI is the first full build.

## Style/completeness slice 3
Logs are in `s1-ui-logs`. Each item says whether it had a genuine red run.
- **Live state restore and whole-buffer meter-mode bit identity** (`state-meter-coverage.log`): passed on first
  run. This is coverage of existing behaviour, not new production TDD. The test calls `setStateInformation` with
  the editor open and checks all four sliders and five toggles through the real attachments. It also uses `memcmp`
  to compare 2x64 samples rendered before and after the meter-mode switch.
- **Peak hold and clip lamps.** `peak-red.log`: compile red, `clipped`/`heldPeakDb` missing. `peak-green.log`:
  PASS. The first green attempt failed (exit 24) because of a test bug: peaks from earlier unpolled renders had not
  been drained. That was fixed in the test only. Replacement policy, **not the measured original**:
  - The bar falls 1.5 dB per 30 Hz tick.
  - The numeric held peak is an infinite hold.
  - The clip lamp latches when the displayed LR or MS signal reaches |x|>=1. In MS mode the lamps reflect M/S,
    not channel clipping.
  - Clicking a lamp or readout resets that channel. A meter-mode change resets both channels.
  - Silence shows `-Inf` and no bar.
- **Alt-click fader reset** (`altclick-coverage.log`): passed on first run. JUCE's
  `Slider::setDoubleClickReturnValue` default Alt single-click modifier provides it, and the test uses real
  `mouseDown`/`mouseUp` events with Alt held. This is coverage, not new TDD. Double-click still works, and the
  display's Alt-drag is on a separate component.
- **Fader caps, scales and anchors.** `fader-red.log`: FAIL code 34, stock dot thumb. `fader-green.log`: PASS. The
  test checks that the runtime snapshot has a broad dark cap with a pale index line at gain y82, at width-1 y274 and
  on the rotation centre. `FaderLook` is an independently drawn `LookAndFeel`:
  - grooves, bevel and drop shadow;
  - travel y82–370 with value boxes at y414;
  - horizontal tracks x365–682 at y427/y512 with readouts at x713;
  - output bars x872–900 / x974–1002, y74–399, with a central shared scale, lamps above and readouts at y414.
  Its lifetime is safe: it is declared before the sliders, and the destructor calls `setLookAndFeel(nullptr)`.
- Re-run: `rt-green-style.log` (0 allocations), `meter-green-style.log` (adapter PASS), `core-ctest-style.log` (3/3).
- Candidate in-process snapshots: `s1-ui-logs/candidates/candidate-{default,width2,rotation20}.png`. These are not
  hosted captures and not pixel parity with the original.

Remaining:
- Hosted Linux/Windows screenshots from CI.
- Root full build validation, plus artifact and release gates.
- Exact original ballistics, projection and gesture sensitivity.
- The genuine original GUI, which still renders black.

## Follow-up slice 4 (after native 9862ef0 review)
The prior head `9862ef0` was verified by the parent on all four native/downloaded jobs, run 40513113985. The new
head's native CI is **pending** and is not accepted.
- **R label overlapped the tick column** in the hosted Linux image. `label-red.log`: compile red, there was no
  geometry accessor yet. `label-green.log`: PASS. Labels now sit above each clip lamp, centred on the bar
  (`channelLabelBounds`). The test asserts they do not intersect either tick column or either bar.
- **Asymmetry readout showed `-24.4035`.** Root cause: `juce::String(double, 0)` means "no fixed precision" in JUCE,
  not zero decimals. It was a genuine formatting bug, not active text editing. `precision-red.log`: runtime FAIL 39.
  Host-style automation set -24.4035 and the readout showed `-24.4035`; Gain, Width and Rotation were already
  correct. Fix: integers are rounded for display only. `precision-green.log`: PASS. The test also asserts the
  parameter keeps its continuous -24.4035.
- **Drawn two-position Input/Meter Mode selectors** (`ModeSwitch`, a real `juce::Button` toggle). Input Mode stays
  bound through the APVTS `ButtonAttachment`. Meter Mode is still display-only. `selector-red.log`: compile red, the
  type did not exist. `selector-green.log`: PASS. The test uses runtime snapshots of the slot to check that the
  knob sits on the L-R side when off and the M-S side when on.
- RT re-run: `rt-green-followup.log`, 0 allocations.
- **Windows full-width capture is not changed.** The runner desktop is 1024 wide and clips about 24 px of the
  1048 body. No supported resolution change was proven within this slice, so this remains an open blocker.

## Linked Asymmetry/Rotation slice 5 (manual p6)
`65c9f23` is still under its own native run, 40516362962. This new head's native CI is **pending** and is not accepted.
- **Behaviour:** dragging the strip between the Asymmetry and Rotation rows (`link_strip`, x365–682, y474–496)
  moves both controls together, like a balance. A purple outline marks the linked pair. Clicking the editor
  background unlinks them.
- **Replacement policy, not measured original sensitivity:** one common normalized delta, `dx / 317 px`
  (the track width, sub-pixel), applied to both. The delta is clamped so neither control leaves its range, which
  preserves their normalized offset. Gestures begin and end only on `asymmetry` and `rotation`. Physical ranges and
  the DSP are unchanged. Modifiers are ignored on the strip, so Alt does not reset. Individual sliders, text boxes,
  the graph drag and the meters are unchanged.
- **TDD:**
  - `link-red.log`: runtime FAIL 44, no link strip.
  - `link-green.log`: FAIL 46. The integer `getDistanceFromDragStartX` rounded 31.7 px to 32.
  - `link-debug.log`: the diagnostic run that printed asym 28.17 / rot 4.085.
  - Fix: a float drag distance. `link-green-2.log` and `link-green-final.log` PASS.
  - The test uses real `MouseEvent`s with Alt held and counts begin/end gestures through an
    `AudioProcessorListener`. It covers boundary clamping with the offset kept (90 / 5), a state restore while
    linked followed by a linked drag from the restored values, and unlinking on a background click.
- RT re-run: `rt-green-link.log`, 0 allocations.
- In-process snapshot (not hosted): `s1-ui-logs/candidates/candidate-linked.png`.
- **Not implemented:** rectangular multi-control selection (manual p8–9). Interaction parity is partial.
- **Windows framing blocker carried:** the 1024-wide runner desktop clips about 24 px of the 1048 body.

## Gesture lifecycle and Windows window capture slice 6 (local commit, native pending)
- **Gesture lifecycle.**
  - `lifecycle-red.log`: runtime FAIL 56, gain begins 2 / ends 0. Closing the editor mid-drag, plus a repeated
    press, left host gestures open.
  - `lifecycle-green.log`: FAIL 57. Gestures were balanced, but a repeated press began a second gesture, which
    contradicts the test's pre-written one-gesture-per-drag intent.
  - `lifecycle-green-2.log`: PASS.
  - `VectorDisplay` and `LinkStrip` now end any open gesture in their destructors. A repeated press while dragging
    is ignored, and `mouseUp` goes through an idempotent `endDrag()`, so there is no double end. Parameter values
    and the RT path are unchanged. `rt-green-lifecycle.log`: 0 allocations.
- **Windows full-body capture.**
  - New `tests/s1/windows_editor_capture.py`. It reuses Pedalboard `load_plugin`/`show_editor` and enumerates this
    process's visible top-level and child windows through pointer-width-safe ctypes signatures. It selects the
    unique 1048x665 client window, requires the exact module to be loaded (`GetModuleHandleW`/
    `GetModuleFileNameW`), records HWND, class, PID and rect before and after, and grabs only that HWND with Pillow
    13 `ImageGrab.grab(window=hwnd)`.
  - It fails closed when no unique window is found, or when the image is smaller than the body or blank. The
    desktop is saved separately as a diagnostic only.
  - The portable selection tests (`test_windows_capture.py`, synthetic records) pass in
    `windows-selection-green.log`. They were written together with the helper, so **no red was witnessed** for them.
  - The helper has **not been executed on Windows yet**. The native Windows CI job is the authoritative proof.

## Original-reference layout calibration slice 7 (local commit, native pending)
The reference is the private static capture of the genuine original 17.1 editor (524x376 frame, about 2x scale
to the 1048x665 logical body, with an 18 px toolbar above).
- **Selectors.** The original Input Mode and Meter Mode selectors share one row, at physical y≈274, which is
  logical y≈512. Both drawn selectors are now centred at y512, with their titles aligned. The polarity/swap row
  (centre y570) and all audio behaviour are unchanged.
- **Width taper.** The original shows Width 1.00 near mid-travel. A slider-only replacement taper,
  `NormalisableRange(0,3).setSkewForCentre(1)`, places 0 at the bottom (y370), 1 at the middle (y226) and 3 at the
  top (y82). It has to replace the attachment's lambda range, because JUCE's `SliderAttachment` installs custom
  conversion lambdas that ignore skew; the first attempt (`setSkewFactorFromMidPoint`) failed for that reason.
  The APVTS parameter stays physical 0..3 and continuous, in both the UI→parameter and automation→UI directions.
  The curve between the anchors is **not** the recovered original law.
- **Provenance of the revised expectation.** The earlier fader test expected Width 1 at y274, a documentation-only
  one-third assumption. It now expects y226, justified by the genuine original capture. This is a correction, not
  retroactive fidelity.
- **TDD:**
  - `original-layout-red.log`: runtime FAIL 35, the width cap was not at mid-travel.
  - `original-layout-green.log`: still FAIL 35 (the skew was ignored by the attachment's lambda range).
  - `original-layout-green-2.log`: PASS.
  - The selector-row assertion (58) was written in the same red batch but was never reached before failure 35, so
    no separate red was witnessed for it.
  - RT re-run `rt-green-original-layout.log`: 0 allocations.
- In-process snapshot (not hosted): `s1-ui-logs/candidates/candidate-default.png`.
