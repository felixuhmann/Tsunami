# Foundation dependency notices

Tsunami-owned source: SPDX **GPL-3.0-or-later**, see `LICENSE`.
This milestone selects **JUCE 9.0.4's AGPLv3 open-source alternative**, not its
commercial terms. Tsunami-authored source remains GPL-3.0-or-later. GPLv3 and
AGPLv3 section 13 permit the combination; AGPL network-interaction requirements
apply to the combination when applicable. See `LICENSES/AGPL-3.0.txt` and
`docs/LICENSING.md`; do not describe the linked distribution as GPL-only.

Exact sources and pins: `dependencies.lock.json`. Retain the **entire matching
source trees**, including embedded copyright/license notices. JUCE's selected
modules include its bundled VST3 SDK, image/font/codecs and
permissively licensed utilities. A top-level license is not a replacement for
those per-file notices. Preserve JUCE 9's upstream `JUCE.spdx.json` inventory;
a reviewed project-level component SBOM remains a release gate.
No external fonts, images, DSP libraries or reference-vendor assets are added.

CLAP JUCE extension (MIT):

Copyright 2019-2020, Paul Walker

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The CLAP module is compiled from a build-local copy of the pinned upstream
`src/wrapper/clap-juce-wrapper.cpp` modified by the checked, hash-pinned
`cmake/patches/clap-juce-wrapper-audio-ports-config.patch` (host-selectable
mono/stereo audio-ports configs). The upstream file stays under the MIT notice
above; the Tsunami-authored patch is GPL-3.0-or-later. The fetched dependency
tree itself is left unmodified.

The MIT CLAP and clap-helpers notices are preserved verbatim in their matching
source trees alongside CI artifacts. Separate pluginval, clap-validator,
Pedalboard, NumPy and Pillow tools are test-only and are **not** shipped linked
inside the plugin. GPL Corresponding Source is required alongside binary
access; CI includes project/dependency source archives, not just moving links.
These are unsigned infrastructure experiments, not releases or matched targets.
