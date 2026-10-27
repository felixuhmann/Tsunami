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
| Frequency value window, "5506" default (fig.; ch.4 "Freq") | Drag value box `freq`, whole Hz, genuine 2000*8^n taper via a slider-side custom NormalisableRange set after the attachment (the earlier skew call was a no-op and was removed) | APVTS `freq` (SliderAttachment); taper maths tested in `deesser_editor_model`; the JUCE harness has not been re-run with the new range | Matches function; the original's exact drag sensitivity is unknown |
| SideChain filter selector with shape icon and drop-down (fig.; ch.4 "SideChain") | ComboBox `sidechain` with BandPass/HighPass text | APVTS `sidechain` (ComboBoxAttachment); tested | Text replaces the original filter-shape icon (gap) |
| Monitor pair "Audio" / "S Chain" + yellow light in SideChain mode (fig.; ch.4 "Monitor") | Two buttons `monitor_audio`/`monitor_sidechain`; painted lamp when S Chain | One parameter `monitor` (ParameterAttachment); tested | Matches |
| Threshold slider with value handle "-43.6", 0..-80, aligned with Energy bar (fig.; ch.4 "Thresh", "Energy Detector") | Vertical slider `threshold` with value text, 0.1 dB steps; shares the 0..-80 scale with the Energy bar | APVTS `threshold`; tested | Matches function; handle shape is approximate |
| Energy Detector bar (blue), scale 0/20/40/60/80, peak readout below (fig.; ch.4) | Blue bar `energy_meter` on the Threshold scale (envelope/sqrt(0.5), the detector's threshold comparison, so attenuation starts when the slider is below the bar peak) + readout with infinite peak hold accumulated in the core between polls, click to reset | Core `Processor::detectorLevelDb()` via `DetectorMeterSource`; tested in the harness | **Data blocked in product build:** the shared Adapter has no detector tap, so the meter is drawn without data |
| Atten bar (red), scale 0/-3/-6/-12/-18/Inf, readout, infinite peak hold, click to reset (fig.; ch.2 step 2; ch.4 "Atten") | Red bar `atten_meter` growing down from 0, tick positions measured from the manual figure (provisional, +-2 px at figure resolution), readout | Core `Processor::gainReductionDb()` via `DetectorMeterSource`; tested in the harness | **Data blocked in product build** (same tap); the scale above 18 dB is provisional |
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
4. The shared Adapter exposes no detector or gain-reduction tap, so Energy and Atten have no data.

The core is now header-only `Processor.hpp`, which matches what the foundation includes.
