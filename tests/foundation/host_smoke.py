#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Pedalboard VST3 smoke assertions, not a DSP-equivalence claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import threading
import time

import numpy as np
import pedalboard
from pedalboard import load_plugin
from PIL import ImageGrab

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('plugin', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
plugin_path = args.plugin.resolve(strict=True)
args.output.mkdir(parents=True, exist_ok=True)
p = load_plugin(str(plugin_path))
assert p.parameters, 'host enumerated no parameters'
parameter = next(iter(p.parameters.values()))
original = parameter.raw_value
parameter.raw_value = .25
saved = p.raw_state
parameter.raw_value = .75
p.raw_state = saved
assert abs(parameter.raw_value - .25) < 1e-5, 'state did not restore parameter'
fresh = load_plugin(str(plugin_path))
fresh.raw_state = saved
assert abs(next(iter(fresh.parameters.values())).raw_value - .25) < 1e-5
renders = []
for rate in (44100, 48000, 96000):
    signal = np.random.default_rng(17).normal(0, .05, (2, 4096)).astype(np.float32)
    for block in (1, 17, 64, 257, 1024):
        y = p(signal, rate, buffer_size=block, reset=True)
        assert y.shape == signal.shape and np.isfinite(y).all()
        assert np.max(np.abs(y)) > 1e-5, 'no nontrivial output'
        p.raw_state = saved
        z = fresh(signal, rate, buffer_size=block, reset=True)
        assert np.allclose(y, z, atol=1e-6), 'fresh instance differs after state restore'
        renders.append(dict(rate=rate, block=block, peak=float(np.max(np.abs(y))), sha256=hashlib.sha256(y.tobytes()).hexdigest()))
parameter.raw_value = original
if args.capture:
    close = threading.Event()
    errors = []
    def capture():
        try:
            time.sleep(3)
            image = ImageGrab.grab()
            assert image.width >= 400 and image.height >= 100
            assert any(lo != hi for lo, hi in image.convert('RGB').getextrema()), 'blank desktop capture'
            image.save(args.output / 'host-desktop.png')
        except BaseException as error:
            errors.append(str(error))
        finally:
            close.set()
    thread = threading.Thread(target=capture)
    thread.start()
    p.show_editor(close)
    thread.join(timeout=10)
    assert not thread.is_alive() and not errors, errors
    assert (args.output / 'host-desktop.png').is_file()
receipt = dict(scope='infrastructure host smoke, NOT target verification or reference fidelity', plugin=str(plugin_path), os=platform.platform(), python=platform.python_version(), pedalboard=pedalboard.__version__, parameters=list(p.parameters), latency=p.reported_latency_samples, renders=renders, capture='full desktop of actual Pedalboard show_editor; human attachment/control review required' if args.capture else 'not requested', source_commit=os.environ.get('GITHUB_SHA'))
(args.output / 'host-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(json.dumps(receipt, indent=2))
