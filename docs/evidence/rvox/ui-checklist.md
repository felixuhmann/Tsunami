# RVox editor UI checklist (hosted capture vs original evidence)

Capture: `hosted-editor-linux-pluginval.png` (371x357), taken from the exact locally
built module `Tsunami RVox.vst3/Contents/x86_64-linux/Tsunami RVox.so`
(sha256 `2d5f17f9f2647ec09e63798c6360764feca9fa83a4e9f8b06de6577e4f3e30f8`), hosted by
pluginval 1.0.4 in-process under Xvfb. Source commit `acbcae6`; the plugin and shared
trees are the same as at `ec03b32`, the commit the module was built from. Receipt:
`hosted-editor-receipt.json`. Log: `hosted-editor-pluginval.log`.

Original evidence (private, not in git): `tsunami-reference/analysis/rvox/ui-reference/`,
including the user guide text (`rvox-manual.txt`) and the guide's Interface page
(`page-3.png`, numbered callouts 1 to 6), plus the 17.1 layout resources used in c642e01.
Nothing from those sources is copied into this repository. Everything in the editor is
painted in code.

This is a manual review only. No numeric fidelity score is given. Status values:
**present**, **partial**, **gap**, **not verified**.

The fader values in the capture are whatever pluginval's Editor Automation (seed 17) had
set at that moment. They are not a chosen preset.

## Controls and displays (guide callouts)

| # | Original element (guide) | In capture | Status | Notes / gaps |
|---|---|---|---|---|
| 1 | Input Energy Meter (left, under the Gate fader; "Energy" value below) | Orange fill and "Energy" value from the core's detector envelope (`editor-live-meters-linux.png`: -6.0 for a -6 dBFS sine) | **partial** | Live via the shared `CoreMeterSource` bridge (merged main; required). Project-defined scale (80 dB span, latest post-block envelope, no extra ballistics), not proven original meter ballistics; see `meter-tap.md`. rvox.editor checks 70/71/75. |
| 2 | Gate Threshold Control, superimposed on the energy meter; value above ("Gate -19.6") | Orange thumb on the left track; "Gate" label with value -54.9 | **present** | Placed on the energy track as in the guide. The scale is labelled 12/24/36/48/60/72. Range is -80 to 0 dB, where -80 is the finite sentinel for the original "-inf/off" (see parameters.json). At the minimum the readout shows "-inf" (rvox.editor check 18). That state does not appear in this capture. |
| 3 | Compression Threshold Control: centre fader, value box on the fader ("-11.3") | Centre panel with an orange value box (-7.3) and a 6 to 36 scale | **present** | Rect 135,99,102,207 is checked by rvox.editor. Double-click value entry is implemented and its unit test passes, but it was not exercised in this capture. |
| 4 | Total Compression Display (large meter; peak GR number at the bottom; click resets peak hold) | Orange bar down from the top on the 40 dB scale and held peak (`editor-live-meters-linux.png`: 18.5 at Comp -24 on a -6 dBFS sine) | **partial** | Live via the shared `CoreMeterSource` bridge (merged main; required): smoothed gate*compressor gain before makeup. Click resets the hold (check 77). Project-defined tap, original meter scale/ballistics unproven; see `meter-tap.md`. |
| 5 | Output Meters (L/R, full scale; peak values at the bottom; click resets peak) | Two orange bars on the right; peaks 14.7 / 13.7 | **partial** | The meters and peak readouts are wired to the adapter MeterSource, and click-to-reset is implemented. **Open question:** the peak readouts show +14.7/+13.7 dB during pluginval's run while the Gain ceiling is -28.7. The guide says the output limiter keeps the output from clipping. This may come from pluginval's non-audio stress inputs or from a real ceiling or meter bug. It is not investigated in this task. |
| 6 | Output Gain Control (Ceiling), on the output meters; value above ("Gain -3.8") | Orange thumb on the output bars; "Gain -28.7" | **present** | Range -36 to 0 dB. |
| – | "RVox" brand title | Painted "RVOX" text in brand red | **partial** | Our own lettering, not the vendor logotype (on purpose: no vendor assets). |
| – | Body: light beige panel with a rounded centre module | Similar light panel with a rounded centre module | **partial** | Only the Light-style look. Panel shading and fonts are approximate and were compared by eye only. |

## Features (guide text)

| Feature | Status | Evidence / gap |
|---|---|---|
| Comp + Gain linking by drawing a box around the faders (they then move together) | **present (unit test)** | rvox.editor checks 22 to 26: a box links comp+gain only, the offset is kept, and a background click unlinks. Only user input moves the linked pair (comp and gain keys, gain value entry, drag, wheel, double-click and alt-click reset; checks 24, 43 to 47 and 49; a key press after unlinking moves comp alone, check 26). Host automation and state recall of one linked parameter leave the other unchanged (checks 40, 41, 42, 48). Not exercised in the hosted capture. Linked follower moves are sent without their own host gesture. |
| Host gestures from fader drags | **present (unit test)** | rvox.editor checks 60 to 62 on all three faders: each drag starts one gesture, and begins and ends stay balanced after a normal release, an editor closed mid-drag, and a repeated press with a duplicate release. These are component events under xvfb, not native-peer input. |
| Double-click a value box to type a value | **present (unit test)** | The value labels become editable on double-click. rvox.editor check 19 enters a gain value. Not exercised in the hosted capture. |
| Option/alt-click resets to default | **present (unit test)** | Uses the JUCE slider double-click-return value, which also responds to alt-click by default. rvox.editor checks 20 and 21 check the reset values; check 49 sends an actual alt-click to the linked gain fader. Not exercised in the hosted capture. |
| Click output meter or peak number to reset peak | **partial** | Output peaks: implemented (rvox.editor check 30). Total Compression peak: implemented with the bridge (check 77, see #4). |
| Skins: Light / Dark / Legacy, chosen from the WaveSystem toolbar; the skin persists as the default for new instances | **gap** | There is only one look (Light-style). There is no WaveSystem toolbar, so no skin selector, preset bar, undo/redo, A/B or toolbar menu either. |
| Mono and stereo components | **partial** | One product offers stereo and opt-in mono 1-in/1-out. Mono is bit-identical to the 1-channel core, with state, meters and layout refusal covered by rvox.adapter checks 60-70. There is no separate "RVox Mono" component name. |
| Ranges: Comp -36..0, Gain -36..0, Gate -inf..0, Output meters -inf..0 | **present / partial** | Comp and Gain match. Gate uses a -80 dB finite sentinel for -inf. The guide's printed "Range: -36 dB to -36 dB" is a typo in the guide, so ranges come from parameters.json and the 17.1 resources. |
| Display resolution of 0.1 dB | **present** | Readouts show one decimal, matching `reference_display_step_db` 0.1. |

## Not covered

- No pixel or visual-equivalence comparison against an original-host screenshot. Only the guide image was viewed. It is a different skin capture at a different scale, so it was not compared numerically.
- Windows hosted editor screenshot: the Windows CI job opens the editor (`host_smoke.py --capture`), but its artifact was not reviewed here.
- Linux CI does not capture the editor yet. This capture was made locally.
