# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Tsunami De-Esser hosted editor capture on Linux (mirrors tests/s1, tests/rvox).

Runs the reviewed foundation pluginval 1.0.4 strictness-6 capture (exact window PID,
WM_CLASS and in-process module mapping) with the DeEsser product identity. The DeEsser view
is 380x380, below the foundation's 400 px window filter, so the committed foundation file's
single `int(width) < 400` literal is substituted at load time with 370 (admits the product,
still rejects stub windows); the load fails if that literal is not present exactly once.
The captured window must be exactly the product dimension (Editor.hpp setSize(380, 380)).

usage: linux_editor_capture.py plugin.vst3 output_dir pluginval
"""
import hashlib, json, sys, types
from pathlib import Path

FOUNDATION = Path(__file__).resolve().parents[1] / 'foundation' / 'linux_editor_capture.py'
NEEDLE = 'int(width) < 400'
MIN_WIDTH = 370
PRODUCT_SIZE = (380, 380)


def load():
    source = FOUNDATION.read_text()
    assert source.count(NEEDLE) == 1, 'foundation window-width filter changed; review this wrapper'
    module = types.ModuleType('linux_editor_capture')
    module.__file__ = str(FOUNDATION)
    exec(compile(source.replace(NEEDLE, f'int(width) < {MIN_WIDTH}'), str(FOUNDATION), 'exec'), module.__dict__)
    return module


def main():
    from PIL import Image
    capture = load()
    capture.TITLE = 'Tsunami De-Esser'
    capture.main()
    output = Path(sys.argv[2])
    receipt = json.loads((output / 'editor-receipt.json').read_text())
    size = Image.open(output / 'host-editor.png').size
    assert size == PRODUCT_SIZE and tuple(receipt['capture']['geometry'][2:]) == PRODUCT_SIZE, f'captured {size}, not the 380x380 product editor'
    receipt['scope'] = ('actual DeEsser target module hosted editor (pluginval 1.0.4, Xvfb); independently drawn manual-figure-style UI, '
                        'NOT original-host visual equivalence')
    receipt['min_window_width'] = MIN_WIDTH
    receipt['product_size'] = list(PRODUCT_SIZE)
    receipt['editor_source_sha256'] = {p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
                                       for p in ('plugins/deesser/Editor.hpp', 'plugins/deesser/EditorModel.hpp',
                                                 'plugins/deesser/Processor.hpp', 'shared/Adapter.hpp',
                                                 'tests/foundation/linux_editor_capture.py')}
    (output / 'editor-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
