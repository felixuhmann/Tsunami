# Tsunami

Independent audio-plugin reimplementations, developed using direct REA-guided analysis and measured comparison with the originals. No Waves code, binaries, branding assets, or license bypasses belong in shipped artifacts.

**Current status: project bootstrap; no target plugin is verified or release-ready.**

Native Windows x64 and Linux x64 are required. Target formats: VST3 and CLAP. UI screenshots, original-reference comparison evidence, automated native-host tests and reproducible release artifacts are required for every plugin PR. Wine is a reference-research convenience, not native Windows acceptance.

## Target list

- CLA-2A → Tsunami Opto (`cla2a`)
- DeEsser → Tsunami De-Esser (`deesser`)
- H-Comp → Tsunami Hybrid Compressor (`hcomp`)
- H-Delay → Tsunami Echo (`hdelay`)
- Renaissance-Compressor → Tsunami Compressor (`rcomp`)
- Renaissance-Reverb → Tsunami Reverb (`rverb`)
- Renaissance-Vox → Tsunami Voice (`rvox`)
- S1-Imager → Tsunami Stereo (`s1`)
- SSL-E-Channel → Tsunami Channel (`sslchannel`)
- WavesTune-RealTime → Tsunami Pitch (`tunert`)
- AphexVintage → Tsunami Exciter (`aphex`)

- NS1 → Tsunami Suppressor (`ns1`)

Track exact requested identifiers in `targets.json`. One implementation PR per target; shared infrastructure may have a separate PR. See `docs/ACCEPTANCE.md` and `docs/PLUGIN_CONTRACT.md`. The NS1 research and implementation effort is included in this 12-plugin bundle; existing private reference evidence stays outside this public repository.
