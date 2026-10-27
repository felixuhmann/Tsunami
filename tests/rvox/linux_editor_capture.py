# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Tsunami RVox hosted editor capture on Linux (mirrors tests/s1).

Reuses the reviewed foundation pluginval strictness-6 capture with the RVox
product identity. The RVox view is 371 px wide, below the foundation's 400 px
window filter, so the minimum width comes from --min-width or the
RVOX_EDITOR_MIN_WIDTH env (default 360). The committed foundation file is not
edited: its single `int(width) < 400` literal is substituted at load time, and
the load fails if that literal is not present exactly once.

usage: linux_editor_capture.py [--min-width N] plugin.vst3 output_dir pluginval
"""
import hashlib, json, os, sys, types
from pathlib import Path

FOUNDATION = Path(__file__).resolve().parents[1] / 'foundation' / 'linux_editor_capture.py'
NEEDLE = 'int(width) < 400'


def load(min_width):
    source = FOUNDATION.read_text()
    assert source.count(NEEDLE) == 1, 'foundation window-width filter changed; review this wrapper'
    module = types.ModuleType('linux_editor_capture')
    module.__file__ = str(FOUNDATION)
    exec(compile(source.replace(NEEDLE, f'int(width) < {int(min_width)}'), str(FOUNDATION), 'exec'), module.__dict__)
    return module


def main():
    argv = sys.argv[1:]
    min_width = int(os.environ.get('RVOX_EDITOR_MIN_WIDTH', '360'))
    if argv[:1] == ['--min-width']:
        min_width, argv = int(argv[1]), argv[2:]
    assert 100 <= min_width <= 371, 'min width must still admit the 371 px RVox editor and reject stubs'
    capture = load(min_width)
    capture.TITLE = 'Tsunami RVox'
    sys.argv = [sys.argv[0], *argv]
    capture.main()
    output = Path(argv[1])
    receipt = json.loads((output / 'editor-receipt.json').read_text())
    receipt['scope'] = ('actual RVox target module hosted editor (pluginval 1.0.4, Xvfb); docs-derived RVox-style UI, '
                        'NOT original-host visual equivalence')
    receipt['min_window_width'] = min_width
    receipt['editor_source_sha256'] = {p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
                                       for p in ('plugins/rvox/Editor.hpp', 'shared/Adapter.hpp',
                                                 'tests/foundation/linux_editor_capture.py')}
    (output / 'editor-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
