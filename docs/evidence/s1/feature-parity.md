<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# S1 Imager feature-parity checklist (manual-grounded, not a numeric fidelity score)

Sources: private official S1 manual. Page numbers are the printed pages (p5–p8, p12–p14, p22). The genuine original
17.1 static editor capture is private (sha256 e5dc30a9…5d69). This is a checklist assessed against the
2026-10-27 merge policy; it does not invent a percentage. Shuffler-only controls (Shuffling, Frequency, Bass Trim;
manual p7) belong to the S1 Shuffler variant and are out of scope for the Imager target.

| Feature (manual) | Status | Evidence |
|---|---|---|
| Gain / Width / Asymmetry / Rotation faders with editable values (p6) | Implemented | `editor_test` attachments, precision and automation checks |
| Input L-R/M-S selector; polarity +/- ×2; channel swap (p7, p12) | Implemented, APVTS-bound, DSP order per p7 | core processor tests; selector drawing test |
| Meter L-R/M-S display mode, no audio change (p13) | Implemented | whole-buffer `memcmp` test |
| Peak/clip indicators with reset (p14) | Implemented. The hold/clip policy is a declared replacement (p14: M/S peak/clip is not reliable) | peak/clip tests |
| Static vector display of L/C/R output positions and levels, not the input signal (p5) | Implemented. The exact original projection is unmeasured | vector model tests |
| Display drag: plain X=Rotation, Y=Gain; Alt X=Asymmetry, Y=Width (p22 key table) | Implemented, with replacement sensitivity | drag tests |
| p8 "drag vertically/horizontally on the display adjusts Gain and Width simultaneously / free-form all four" | Follows the p22 key table. Wording on p8 is internally inconsistent with p22, so p22 governs | — |
| Drag the space between Asymmetry and Rotation as a balance (p6) | Implemented (link strip) | link tests |
| Select both, then drag one (p6) / draw a box around contiguous controls to link them, offsets maintained (p8) | Implemented for both contiguous pairs: Asymmetry+Rotation and Gain+Width. Press outside unlinks | lasso and vertical-pair tests |
| Alt+click resets an individual control (p8) | Implemented | Alt-click test |
| Reset of Width/Asymmetry/Rotation keeping gain/polarity/swap (p7) | Implemented as a replacement action (R key, double-click on display); no invented body button | reset-image test |
| "S1 Full Reset" preset via the WaveSystem toolbar (p8) | Host preset/state mechanism only; no vendor toolbar replica | state tests |
| Resize (wrapper) | Implemented, 50–125 % uniform | resize tests |

Remaining functional items:
- Box selection spanning across panels, such as Width with Asymmetry, is not treated as contiguous and is not linked.
- Real `ComponentPeer` dispatch of child presses is unproven locally (Xvfb without a WM). Native hosts were not
  exercised interactively.
- Original drag sensitivities and ballistics are unmeasured. The merge policy says this does not block on its own.
