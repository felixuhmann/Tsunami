# Trying Tsunami preview plugins in a DAW

SPDX-License-Identifier: GPL-3.0-or-later

**These are experimental, unsigned preview builds.** They are produced by the
`Release packages (preview)` GitHub Actions workflow. They are not verified as
equivalent to any original product and are not a stable release.

## What is currently available

Only targets with a native adapter registered in the root `CMakeLists.txt` are
built. On the current `main` that is **one** of the twelve targets:

| Product | Plugin id | Formats | Platforms |
|---|---|---|---|
| Tsunami Stereo (S1-Imager replacement) | `s1` | VST3, CLAP | Windows x64, Linux x64 |

The other eleven targets are not built yet. Each workflow run lists them with
the reason in `unavailable.json` (in the `release-discovery` artifact). There
is **no macOS build**.

No vendor software, account or license is needed. These plugins are
independent replacements and do not load or need the originals.

## Download

1. Open the repository's **Actions** tab, then pick a green run of
   **Release packages (preview)**.
2. Download the artifact for your platform, e.g.
   `Tsunami-Stereo-preview-<run>-<commit>-windows-x64` or
   `...-linux-x64`, and unzip it once (GitHub wraps artifacts in a zip). It
   contains the package zip `Tsunami-Stereo-…-<platform>.zip` and its
   `.sha256` file.
3. Optional check: `sha256sum -c <package>.zip.sha256` (Linux), or on Windows
   `Get-FileHash <package>.zip` and compare with the `.sha256` file.
4. Unzip the package. It contains:
   - `VST3/Tsunami Stereo.vst3` — a **folder** (VST3 bundle), keep it whole
   - `CLAP/Tsunami Stereo.clap` — a single file
   - `MANIFEST.json` (exact commit, version, platform, hashes),
     `SHA256SUMS`, `licenses/` (project and upstream third-party license
     files) and this guide.

Maintainers may also create draft prereleases from `main`. Treat them the same
way. The checksums detect corrupted downloads. They do not prove who built the
files, because they come from the same place as the files.

## Install on Windows (x64)

- **VST3:** copy the entire `Tsunami Stereo.vst3` folder, not only the file
  inside it, to `C:\Program Files\Common Files\VST3\`. Windows asks for
  administrator rights for that folder.
- **CLAP** (only if your DAW explicitly supports CLAP):
  copy `Tsunami Stereo.clap` to `C:\Program Files\Common Files\CLAP\`. Create
  the folder if it does not exist.
- The build is unsigned. Windows SmartScreen or antivirus software may warn
  about it.
- If the DAW reports a missing `VCRUNTIME`/`MSVCP` DLL, install the Microsoft
  Visual C++ 2015-2022 x64 redistributable.

## Install on Linux (x64)

```sh
mkdir -p ~/.vst3 ~/.clap
cp -r "VST3/Tsunami Stereo.vst3" ~/.vst3/
cp "CLAP/Tsunami Stereo.clap" ~/.clap/
```

System-wide locations `/usr/lib/vst3` and `/usr/lib/clap` also work. The
modules are built on Ubuntu 22.04, so they need glibc 2.35 or newer and the
usual desktop libraries: X11, freetype, fontconfig and ALSA.

## Use it in your DAW

1. Restart the DAW or run its plugin rescan. In REAPER use *Preferences →
   Plug-ins → VST → Re-scan*. In Bitwig check *Settings → Locations*.
2. Look for **Tsunami Stereo** by the manufacturer **Tsunami**. It is an
   **audio effect**, not an instrument. Insert it on a stereo audio track or
   bus, not on an instrument/MIDI track slot.
3. Start with a test session at low monitoring volume. Experimental plugins
   can produce unexpected output.
4. Only 64-bit x64 hosts are supported. 32-bit or ARM hosts will not load these
   modules.

If the DAW does not list the plugin, check that the whole `.vst3` folder was
copied, the DAW scans that folder, and your DAW and OS are x64.

## Source code and licenses

Tsunami's own source is GPL-3.0-or-later. The binaries link JUCE 9.0.4 under
its AGPLv3 option, see `licenses/`. Each package's `MANIFEST.json` names the
companion **Corresponding Source** archive (`Tsunami-source-<version>.tar.gz`)
by exact SHA-256. It contains this project at the exact built commit and every
fetched dependency source tree (JUCE, clap-juce-extensions with its CLAP
submodules), along with `BUILDING.md` with rebuild steps from the bundled sources (that
offline rebuild path is documented but not yet exercised in CI). It is
published next to the binaries as the `Tsunami-source-<version>` artifact.

## What the CI checks

Before upload, each package is built natively on Windows Server 2022 and
Ubuntu 22.04. CI runs the project's tests, packages the modules and loads the
VST3 in Pedalboard. A separate job downloads the uploaded artifacts. It checks
the hashes, the manifest and the source identity. It then loads the downloaded
VST3 and checks finite, non-silent processing at several sample rates and block
sizes, and a state round trip. That is a packaging smoke test, not evidence of
sound equivalence. CLAP modules are packaged but not hosted in that smoke test.

Default settings can leave a plugin passing audio through unchanged. The De-Esser
threshold defaults to 0 dB, and the Voice compression defaults to 0 dB. So the
smoke test also runs a fixed check for each product. It sets named controls to a
neutral value and to an active value, reads them back from the host and renders
fixed test audio both ways through the same plugin:

| Product | Neutral | Active | Test audio | Required effect |
| --- | --- | --- | --- | --- |
| Tsunami Stereo | width 1.0 | width 2.4 | different tones in left and right | side/mid ratio up at least 3 dB |
| Tsunami De-Esser | threshold 0 dB | threshold -36 dB | 300 Hz plus 7 kHz tone | 7 kHz down at least 3 dB, 300 Hz within 1.5 dB |
| Tsunami Voice | compression 0 dB | compression -18 dB | tone switching loud and quiet | loud/quiet difference down at least 3 dB |

The Voice check keeps the gate at -80 dB (off) and the gain at 0 dB in both runs.

Pedalboard compensates for the latency that a plugin reports. A plugin can
report a different latency at another sample rate, but Pedalboard only applies
the new value after the first render at that rate. So the check renders once at
each rate before measuring. It then checks that a noise signal comes out with
no delay at both the neutral and the active setting. It compares the neutral and
active outputs, not the input.

A fresh instance must restore the active state and produce the same audio. A
package for a product without a check fails the smoke test. It does not reuse
another product's check. To release a newly registered target, first add its
check to `tools/release_smoke.py`. These checks show that the downloaded package
processes audio when its controls are set. They are not evidence of sound
equivalence.
