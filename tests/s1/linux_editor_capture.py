# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Tsunami Stereo (S1) hosted editor capture on Linux.

Reuses the reviewed foundation pluginval strictness-6 capture unchanged, with the
target product identity instead of the fixture, then writes an S1-scoped receipt.
"""
import hashlib, json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'foundation'))
import linux_editor_capture as capture  # noqa: E402

capture.TITLE = 'Tsunami Stereo'
capture.main()
output = Path(sys.argv[2])
receipt = json.loads((output / 'editor-receipt.json').read_text())
receipt['scope'] = ('actual S1 target module hosted editor (pluginval 1.0.4, Xvfb); docs-derived original-style UI, '
                    'NOT original-host visual equivalence')
receipt['editor_source_sha256'] = {p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
                                   for p in ('plugins/s1/Editor.hpp', 'plugins/s1/EditorModel.hpp', 'shared/Adapter.hpp')}
(output / 'editor-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
