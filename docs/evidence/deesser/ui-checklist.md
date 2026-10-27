# DeEsser UI parity checklist

Original evidence: the DeEsser user manual shipped in the reference bundle
(`DeEsser.pdf`, ch.2 Quick Start, ch.4 "DeEsser Controls" panel figure and text),
plus the genuine parameter enumeration in `plugins/deesser/parameters.json`.
The Tsunami editor (`plugins/deesser/Editor.hpp`) is drawn in code. No vendor
bitmaps, fonts or GUI XML are used. No numeric similarity score is claimed.
No pixel capture of either editor has been compared.

| Original element (evidence) | Tsunami editor | Binding / data | Status |
|---|---|---|---|
| Audio button, shows "Split"/"Wideband" (fig.; ch.4 "Audio") | Toggle button `audio`, label shows the current mode | APVTS `audio` (ButtonAttachment); tested both ways | Matches function; look approximate |
| Frequency value window, "5506" default (fig.; ch.4 "Freq") | Numeric value window `freq` (white framed box, whole-Hz text, no fill bar since 35b870a), vertical drag, click to type, keyboard/wheel via the JUCE slider; genuine 2000*8^n taper via a slider-side custom NormalisableRange set after the attachment | APVTS `freq` (SliderAttachment); taper maths tested in `deesser_editor_model`; adapter test 49 (no value-proportional fill), 50 (nonintegral host value kept, display-only rounding), 51 (drag: one balanced gesture, other parameters unchanged), 52/53 (typed entry, nonfinite text rejected) | Matches function (documentation-derived); the original's exact drag sensitivity is unknown |
| SideChain filter selector with shape icon and drop-down (fig.; ch.4 "SideChain") | ComboBox `sidechain` with BandPass/HighPass text (compact 12 pt, 78 px; adapter test 37-39) plus an independently drawn vector filter-shape pictogram `sidechain_shape` under it (HighPass: rise then flat; BandPass: single hump). The pictogram is a button: click/keyboard selects the other shape; title/tooltip "SideChain filter: <name>" | APVTS `sidechain` (ComboBoxAttachment + ParameterAttachment); adapter test 54 (follows host change and state recall while open), 55 (click = one complete gesture, combo follows, other parameters unchanged) | Matches function (documentation-derived). Icon below rather than inside the selector; shape drawn from the manual text, not traced from vendor art |
| Monitor pair "Audio" / "S Chain" + yellow light in SideChain mode (fig.; ch.4 "Monitor") | Two buttons `monitor_audio`/`monitor_sidechain`; painted lamp when S Chain | One parameter `monitor` (ParameterAttachment); tested | Matches |
| Threshold slider with value handle "-43.6", 0..-80, aligned with Energy bar (fig.; ch.4 "Thresh", "Energy Detector") | Vertical slider `threshold` with value text, 0.1 dB steps; shares the 0..-80 scale with the Energy bar | APVTS `threshold`; tested | Matches function; handle shape is approximate |
| Energy Detector bar (blue), scale 0/20/40/60/80, peak readout below (fig.; ch.4) | Blue bar `energy_meter` on the Threshold scale (envelope/sqrt(0.5), the detector's threshold comparison, so attenuation starts when the slider is below the bar peak) + readout with infinite peak hold accumulated in the core between polls, click to reset | Product build: core `meter(0)` (latest post-block value) via the shared `CoreMeterSource`; harness: `DetectorMeterSource`; tested in the real Adapter test | Live in the product build; not genuine-original ballistics; the 30 Hz poll samples the latest block only |
| Atten bar (red), scale 0/-3/-6/-12/-18/Inf, readout, infinite peak hold, click to reset (fig.; ch.2 step 2; ch.4 "Atten") | Red bar `atten_meter` growing down from 0, tick positions measured from the manual figure (provisional, +-2 px at figure resolution), readout | Product build: core `meter(1)` via `CoreMeterSource`; tested in the real Adapter test | Live in the product build; the scale above 18 dB is provisional |
| Output L/R bars (orange), -30..0 dBFS, peak readouts, click to reset (fig.; ch.4 "Output") | Two orange bars + readouts, infinite peak hold | Shared `tsunami::MeterSource::takeOutputPeaks()` | Matches function; ballistics/peak-hold decay of the original are not documented |
| Bypass (genuine parameter index 5) | No panel control. The original panel has none (host/WaveShell toolbar). | APVTS `bypass`, host only, saved with the state | Matches the original panel |
| WaveShell toolbar (undo/redo, A/B, Setup, Save, preset menu) | Not drawn | n/a | Out of scope (vendor shell feature) |
| "DeEsser" footer branding | Painted text "DeEsser" | n/a | Approximate (vendor font not used) |
| State save/restore | APVTS state is saved and restored, and the UI follows | APVTS round trip in the JUCE harness (the product path). `ui::ParameterState` is a test-only serializer that the product does not use | Tested |

The coordinator mentioned a Range control. It is **not** in the genuine parameter list or
the manual, so it was not added (enforced in `tests/deesser/test_editor_model.cpp`).

Foundation gaps that block building this editor into the product plugin. All of them are
outside the deesser scope:
1. `tools/generate_metadata.py` rejects `logarithmic: true` (Freq).
2. The generator emits step 0 for `choice` parameters.
3. The root CMake only integrates `s1`.
4. Energy/Atten data in the product build is live since c6f406c (core `meter()` via the shared `CoreMeterSource`; current post-block values, not genuine-original ballistics). Previously: no detector tap.

The core is now header-only `Processor.hpp`, which matches what the foundation includes.

Readout and label fixes at 948e848, regressions in `tests/deesser/adapter_test.cpp` (RED witnessed for 36, 39, 40 and 41; 37/38 are guards that passed before the fix):
- Initial Threshold/Freq value-box text uses the editor's own text (36).
- Both SideChain names show whole (37-39).
- Readouts never show `-0.0` (40).
- Before the first poll, meters are idle: empty Energy/Output bars and Atten `0.0` (41).

Frequency/SideChain controls at 35b870a, regressions 49-58 in `tests/deesser/adapter_test.cpp`
(RED witnessed for 49, 52, 54, 56, 57 and 58; 50/51/53/55 are behaviour guards):
- 49: the bar-style slider drew a value-proportional fill behind the number (overlap seen in the hosted capture); now only a framed window.
- 52: typing `nan` into the Frequency box set the host `freq` parameter to NaN (state corruption).
- 57 (bcc5c09): typed `inf` still moved Frequency 5586 -> 16000 with a host gesture (the host parser clamps before the
  old guard). Text whose raw leading number is nonfinite (`nan`, `-NaN`, `inf`, `-inf`, `1e999`, `-1e999`) now keeps the
  exact current value before the host parser runs: parameters, serialised state, slider value, canonical text and gesture
  counts unchanged (Frequency and Threshold, non-boundary nonintegral starting values). Other text keeps the host parser's
  loose behaviour; this is not a full grammar validation.
- 58 (bcc5c09): an accessibility-style value set (`ScopedDragNotification` + `setValue`, which is exactly what JUCE's slider
  accessibility value interface does) with NaN set host `freq` to NaN. The slider ranges are wrapped (law, snap and
  interval unchanged) so NaN/+-Inf keep the current value; finite entry still works. The real handler needs a native peer,
  which the headless xvfb run (no window manager) cannot create, so the test calls the same public API directly.
- 54/55: SideChain filter-shape pictogram.
- Known gap: tooltips are not displayed (no TooltipWindow); titles still reach accessibility.
- 56 (02ca890): closing the editor during a Frequency drag left the host gesture open (begins 1, ends 0); the destructor
  now releases a dragging slider while its attachment still listens.
Hosted Linux capture of the 02ca890 module (pluginval 1.0.4 strictness 6 SUCCESS, Xvfb, 380x380, Frequency "2428" in the
value window, BandPass pictogram): `state/deesser-ui-controls/ui-linux-02ca890/` outside the repository (module sha256
80f0ac96..., screenshot 338e1d65..., Editor.hpp sha256 453cc5e4...).
Not yet captured on Windows at this head. Not compared with the original host's pixels.
