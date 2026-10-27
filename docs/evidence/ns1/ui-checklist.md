# NS1 UI parity checklist

The original evidence comes from three sources. The first is the NS1 user guide shipped in the reference bundle
(`NS1.pdf`): ch.2 says "Move the fader up to suppress noise", and ch.3 "Interface and
Controls" has a panel figure plus the FADER and ATTENUATION METER text. The second is the
shipped `ParamXML/1000.xml`, which was read only and not copied: `PARAM_WET_DRY_CNTRL`
(Label `1Knob`, 0..100 %, Resolution 0.1, linear, initial 0.0, DisplayFloat1) and
`PARAM_ATTN_VU` (Label `AttenVU`, -48..0 dB, Resolution 0.1, display only). The third is
the genuine parameter readback recorded in `plugins/ns1/parameters.json`.

The Tsunami editor (`plugins/ns1/Editor.hpp`) is drawn in code. It uses no vendor bitmaps,
fonts, logo or GUI XML. No numeric similarity score is claimed, and no pixel capture of
either editor has been compared.

| Original element (evidence) | Tsunami editor | Binding / data | Status |
|---|---|---|---|
| Vertical fader, scale 0..100 in steps of 10 on both sides (ch.3 figure; ch.2/ch.3 text) | `juce::Slider` LinearVertical `ns1`, scales 0..100 painted on both sides from `ui::faderFraction` | APVTS `ns1` (SliderAttachment); range, 0.1 % quantisation, linear taper and default 0 tested in `tests/ns1/test_editor_model.cpp` | Function matches. The cap is a code-drawn ribbed dark cap with an orange index line, so its look is only approximate. Drag sensitivity of the original is unknown. |
| Fader value window below the fader, "41.0" (figure) | Label `ns1_readout`, black with amber text, DisplayFloat1 text. Double-click to type a value. | `ui::valueText(0, v)`, tested ("41.0", "100.0") | Function matches. Whether the original window accepts typed input is not documented. |
| Attenuation meter, orange bar, scales 0..-48 dB in 6 dB steps on both sides, inside a recessed panel (figure; ch.3 "ATTENUATION METER") | `attenuation_meter` bar in a recessed rounded panel, scales painted from `ui::attenuationFraction` (linear, 0..-48) | `Processor::attenuationDb()` (lock-free `std::atomic<float>`), polled via `ui::MeterModel`, tested | **Product data blocked:** the shared Adapter has no attenuation tap. The editor reads the plugin-local `AttenuationMeterSource` and, without it, draws the meter with no data. Meter quantity: block energy ratio out/in in dB (provisional). Ballistics are not recovered. Bar fill direction (from -48 up to the reading) is read from the figure. |
| Meter value window "-46.9" (figure) | `attenuation_readout`, one decimal | `ui::meterText`, tested | Shows the current reading. The figure shows -46.9 while the bar sits higher, so the original window may hold a peak or minimum. This is not established. |
| Bypass (genuine parameter index 1) | No panel control. The original panel has none (WaveShell toolbar / host). | APVTS `bypass`, host only, saved with the state. The meter reads 0 dB while bypassed (tested). | Matches the original panel |
| "NS1 / Noise Suppressor" product text, bottom left (figure) | Painted text "NS1" and "Noise Suppressor" | n/a | Approximate. The vendor wordmark, the boxed "1" and the font are not reproduced. |
| "Waves Post Production" badge, top left (figure) | Not drawn | n/a | Omitted on purpose (vendor branding) |
| WaveSystem toolbar (undo/redo, Setup A/B, A->B, Load, Save, ?) (figure; ch.1.4) | Not drawn | n/a | Out of scope (vendor shell feature) |
| Gradient grey body (figure) | Painted vertical gradient | n/a | Approximate colours chosen by eye, not sampled |
| State save/restore | APVTS state. `ui::ParameterState` save/restore is all-or-nothing and rejects foreign, unknown-schema, garbled and unknown-parameter input. | tested | Tested in the model. No JUCE harness round trip yet. |

## Not verified

- `Editor.hpp` compiles with `g++ -fsyntax-only -Wall -Wextra` against JUCE module headers.
  It has not been instantiated in a JUCE harness, and it has not been built into the product plugin.
- The ParamXML also lists internal display-only items (`PARAM_ATTN_VU_1..6`, `PARAM_IN_VU_*`,
  `PARAM_OUT_VU_*` with `DisplayInControlSurface`, the graph params) that the manual's
  panel does not show. No editor controls were added for them.

## Foundation gaps (outside the ns1 scope)

1. The shared Adapter exposes no attenuation or gain-reduction tap, so in a product build the meter has no data.
2. The root CMake does not integrate `plugins/ns1`.
