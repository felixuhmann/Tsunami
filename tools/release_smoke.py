#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release-package smoke: load a verified, extracted package's VST3 in Pedalboard.

Checks module hash against MANIFEST.json before loading, then finite non-silent
processing across rates/blocks, a state round trip into a fresh instance and
deterministic output. This is packaging evidence, NOT original-equivalence evidence.
"""
import argparse
import hashlib
import json
import platform
import sys
from pathlib import Path

import numpy as np
import pedalboard
from pedalboard import load_plugin

RATES = (44100, 48000, 96000)
BLOCKS = (1, 64, 480, 1024)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='extracted package directory (output of release.py verify)')
    parser.add_argument('receipt', type=Path)
    a = parser.parse_args()
    root = a.package.resolve(strict=True)
    manifest = json.loads((root / 'MANIFEST.json').read_text(encoding='utf-8'))
    module = (root / manifest['vst3_module']).resolve(strict=True)
    bundle = (root / manifest['formats']['vst3']).resolve(strict=True)
    if root not in module.parents or not module.is_file():
        raise SystemExit('manifest module escapes package')
    module_sha = hashlib.sha256(module.read_bytes()).hexdigest()
    if module_sha != manifest['vst3_module_sha256']:
        raise SystemExit('module hash differs from manifest; refusing to load')
    # Pedalboard's Windows scanner needs the PE module inside the same bundle.
    host_path = module if platform.system() == 'Windows' else bundle
    print('Loading', host_path, flush=True)
    host = load_plugin(str(host_path))
    params = [(k, v) for k, v in host.parameters.items() if 'bypass' not in k.lower()]
    if not params:
        raise SystemExit('host enumerated no non-bypass parameters')
    name, parameter = params[0]
    default_state = host.raw_state
    parameter.raw_value = .25
    saved = host.raw_state
    parameter.raw_value = .75
    host.raw_state = saved
    assert abs(parameter.raw_value - .25) < 1e-5, 'state did not restore parameter'
    fresh = load_plugin(str(host_path))
    fresh.raw_state = saved
    assert abs(fresh.parameters[name].raw_value - .25) < 1e-5, 'fresh instance did not restore state'
    host.raw_state = default_state
    fresh.raw_state = default_state
    rng = np.random.default_rng(29)
    n = 8192
    t = np.arange(n) / 48000.0
    signal = (rng.normal(0, .05, (2, n)) + .2 * np.stack([np.sin(2 * np.pi * 440 * t), np.sin(2 * np.pi * 660 * t)]))
    signal = signal.astype(np.float32)
    cases = []
    for rate in RATES:
        for block in BLOCKS:
            y = host(signal, rate, buffer_size=block, reset=True)
            z = fresh(signal, rate, buffer_size=block, reset=True)
            assert y.shape == signal.shape, (rate, block, y.shape)
            assert np.isfinite(y).all(), f'non-finite output at {rate}/{block}'
            peak = float(np.max(np.abs(y)))
            assert peak > 1e-4, f'silent output at {rate}/{block}'
            assert np.allclose(y, z, atol=1e-6), f'fresh instance differs at {rate}/{block}'
            cases.append({'rate': rate, 'block': block, 'peak': peak,
                          'rms': float(np.sqrt(np.mean(y.astype(np.float64) ** 2))),
                          'differs_from_input': bool(np.max(np.abs(y - signal)) > 1e-6),
                          'output_sha256': hashlib.sha256(y.tobytes()).hexdigest()})
    silent = host(np.zeros_like(signal), 48000, buffer_size=256, reset=True)
    assert np.isfinite(silent).all(), 'non-finite output from silence'
    receipt = {
        'scope': 'release-package smoke of the downloaded package; NOT original-equivalence or fidelity evidence',
        'package': root.name, 'id': manifest['id'], 'product': manifest['product'], 'platform': manifest['platform'],
        'version': manifest['version'], 'commit': manifest['commit'], 'host_os': platform.platform(),
        'python': platform.python_version(), 'pedalboard': pedalboard.__version__,
        'vst3_module_sha256': module_sha, 'clap_sha256': manifest['clap_sha256'], 'clap_hosted': False,
        'parameters': list(host.parameters), 'state_parameter': name, 'state_roundtrip': True,
        'latency_samples': host.reported_latency_samples, 'cases': cases,
        'silence_peak': float(np.max(np.abs(silent))),
    }
    a.receipt.parent.mkdir(parents=True, exist_ok=True)
    a.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({k: receipt[k] for k in ('package', 'commit', 'vst3_module_sha256', 'state_roundtrip')}, indent=2))
    print(f'{len(cases)} rate/block cases passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
