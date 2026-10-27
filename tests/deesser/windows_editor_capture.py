# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Tsunami De-Esser editor window capture on native Windows (mirrors tests/rvox approach).

Runs the reviewed S1 Windows capture (Pedalboard show_editor of the exact VST3 module, visible
window of this PID with the exact client size, in-process module check, read-back desktop
mode, fail closed) with the DeEsser product body. The committed S1 file is not edited: its
single `BODY = (1048, 665)` literal is substituted at load time with the DeEsser 380x380 body
(Editor.hpp setSize); the load fails if that literal is not present exactly once.

usage: windows_editor_capture.py plugin.vst3 output_dir
"""
import hashlib, json, sys, types
from pathlib import Path

S1 = Path(__file__).resolve().parents[1] / 's1' / 'windows_editor_capture.py'
NEEDLE = 'BODY = (1048, 665)'
PRODUCT_SIZE = (380, 380)


def load():
    source = S1.read_text()
    assert source.count(NEEDLE) == 1, 'S1 capture body literal changed; review this wrapper'
    module = types.ModuleType('windows_editor_capture')
    module.__file__ = str(S1)
    exec(compile(source.replace(NEEDLE, f'BODY = {PRODUCT_SIZE}'), str(S1), 'exec'), module.__dict__)
    return module


def main():
    capture = load()
    assert capture.BODY == PRODUCT_SIZE
    capture.main()
    output = Path(sys.argv[2])
    path = output / 'windows-editor-receipt.json'
    receipt = json.loads(path.read_text())
    assert tuple(receipt['image_size']) == PRODUCT_SIZE, receipt['image_size']
    receipt['scope'] = ('actual DeEsser module editor client rectangle (Pedalboard show_editor, native Windows); independently drawn '
                        'manual-figure-style UI, NOT original-host visual equivalence')
    receipt['product_size'] = list(PRODUCT_SIZE)
    receipt.pop('prior_failure', None)  # S1-specific history, not a DeEsser run
    receipt['editor_source_sha256'] = {p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
                                       for p in ('plugins/deesser/Editor.hpp', 'plugins/deesser/EditorModel.hpp',
                                                 'plugins/deesser/Processor.hpp', 'shared/Adapter.hpp',
                                                 'tests/s1/windows_editor_capture.py')}
    path.write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
