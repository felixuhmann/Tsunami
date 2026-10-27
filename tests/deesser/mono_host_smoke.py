#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native mono 1-in/1-out smoke of the actual DeEsser VST3 in a mature host (Pedalboard).

Loads the exact module and processes a (1, N) buffer. Checks that the host really ran the plugin
mono (output shape (1, N)) and that the output matches the 1-channel portable core renderer
(so it is not a stereo plugin fed a duplicated/downmixed signal). Also checks active de-essing at
Threshold -40 dB, transparency at 0 dB, a state round trip, and that stereo (2, N) still matches
the 2-channel core. Not a fidelity claim against the original plugin.

usage: mono_host_smoke.py plugin.vst3 deesser_render output_dir
"""
import argparse
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path

import numpy as np
import pedalboard
from pedalboard import load_plugin

RATE, N, BLOCK = 48000, 16 * 512, 512
# Physical controls in Processor index order (freq, threshold, sidechain, audio, monitor, bypass).
CONTROLS = (5506.0, -40.0, 1.0, 0.0, 0.0, 0.0)


def render_core(renderer, planar):
    channels = planar.shape[0]
    args = [str(renderer), str(RATE), str(channels), str(N), str(BLOCK), *(repr(v) for v in CONTROLS)]
    result = subprocess.run(args, input=np.ascontiguousarray(planar, dtype=np.float32).tobytes(), capture_output=True, check=True)
    return np.frombuffer(result.stdout, dtype=np.float32).reshape(channels, N)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('renderer', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    plugin_path = args.plugin.resolve(strict=True)
    if platform.system() == 'Windows' and plugin_path.is_dir():
        plugin_path = (plugin_path / 'Contents' / 'x86_64-win' / plugin_path.name).resolve(strict=True)
    module = plugin_path
    if plugin_path.is_dir():
        module = (plugin_path / 'Contents' / 'x86_64-linux' / (plugin_path.stem + '.so')).resolve(strict=True)
    renderer = args.renderer.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)

    p = load_plugin(str(plugin_path))
    threshold = p.parameters['threshold_db']  # Pedalboard key: name + unit
    threshold.raw_value = 0.5  # -40 dB on the linear -80..0 dB range
    assert abs(threshold.raw_value - 0.5) < 1e-6, 'threshold readback'

    x = (0.5 * np.sin(2 * np.pi * 7000 * np.arange(N) / RATE)).astype(np.float32)[None, :]
    y = p(x, RATE, buffer_size=BLOCK, reset=True)
    assert y.shape == (1, N), f'host did not run the plugin mono 1-in/1-out: output shape {y.shape}'
    assert np.isfinite(y).all(), 'non-finite mono output'
    mono_ref = render_core(renderer, x)
    mono_diff = float(np.max(np.abs(y - mono_ref)))
    assert mono_diff <= 1e-6, f'mono output differs from the 1-channel core by {mono_diff}'
    attenuation_db = float(10 * np.log10(np.sum(x.astype(np.float64) ** 2) / np.sum(y.astype(np.float64) ** 2)))
    assert attenuation_db >= 3.0, f'no active de-essing in mono ({attenuation_db:.2f} dB)'

    saved = p.raw_state
    threshold.raw_value = 1.0  # 0 dB: no attenuation; Split mode then reconstructs the input
    transparent = p(x, RATE, buffer_size=BLOCK, reset=True)
    assert transparent.shape == (1, N) and np.allclose(transparent, x, atol=1e-5), 'mono not transparent at Threshold 0 dB'
    p.raw_state = saved
    assert abs(threshold.raw_value - 0.5) < 1e-6, 'state did not restore Threshold in mono'
    again = p(x, RATE, buffer_size=BLOCK, reset=True)
    assert np.array_equal(again, y), 'mono render not reproducible after state restore'

    xs = np.vstack([x[0], 0.5 * x[0]]).astype(np.float32)
    ys = p(xs, RATE, buffer_size=BLOCK, reset=True)
    assert ys.shape == (2, N) and np.isfinite(ys).all(), f'stereo processing broken: {ys.shape}'
    stereo_diff = float(np.max(np.abs(ys - render_core(renderer, xs))))
    assert stereo_diff <= 1e-6, f'stereo output differs from the 2-channel core by {stereo_diff}'

    receipt = dict(scope='actual DeEsser VST3 mono 1-in/1-out and stereo in Pedalboard vs the portable core renderer; '
                         'NOT original-plugin fidelity, NOT a CLAP mono claim',
                   plugin=str(plugin_path), module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
                   renderer_sha256=hashlib.sha256(renderer.read_bytes()).hexdigest(), pid=os.getpid(),
                   os=platform.platform(), python=platform.python_version(), pedalboard=pedalboard.__version__,
                   rate=RATE, frames=N, block=BLOCK, controls=CONTROLS, mono_shape=list(y.shape),
                   mono_max_diff_vs_core=mono_diff, mono_attenuation_db=attenuation_db, stereo_max_diff_vs_core=stereo_diff,
                   mono_output_sha256=hashlib.sha256(y.tobytes()).hexdigest(), source_commit=os.environ.get('GITHUB_SHA'))
    (args.output / 'mono-host-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
