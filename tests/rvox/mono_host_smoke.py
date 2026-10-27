#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""RVox VST3 hosted with 1-in/1-out input in Pedalboard (JUCE VST3 host), on the native OS.

Pedalboard negotiates the plugin's main bus to the input channel count and raises
"does not support 1-channel output" when the plugin refuses that layout (a stereo-only
Tsunami product does), so a (1, N) result means the host selected a mono bus here.
Reuses the reviewed RVox active-processing profile from tools/release_smoke.py:
physical control readback, active compression vs neutral, latency alignment and
fresh-instance state/audio recall, all with mono input. Stereo processing through the
same module is rechecked afterwards. VST3 only: this is NOT CLAP mono evidence and NOT
original-equivalence evidence.
"""
import argparse
import hashlib
import json
import os
import platform
import sys
from pathlib import Path

import numpy as np
import pedalboard
from pedalboard import load_plugin

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import release_smoke as rs  # noqa: E402

PROFILE = rs.PROFILES['rvox']


def mono(x):
    return np.ascontiguousarray(x[:1])


def fail(message):
    raise SystemExit(f'mono host smoke: {message}')


def render_ok(label, y, x):
    if y.shape != x.shape or not np.isfinite(y).all() or float(np.max(np.abs(y))) <= 1e-3:
        fail(f'{label}: output shape {y.shape} for input {x.shape}, finite={bool(np.isfinite(y).all())}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path, help='built "Tsunami Voice.vst3" bundle')
    parser.add_argument('output', type=Path)
    a = parser.parse_args()
    bundle = a.plugin.resolve(strict=True)
    host_path = bundle
    if platform.system() == 'Windows' and bundle.is_dir():  # Pedalboard needs the PE module in this bundle
        host_path = (bundle / 'Contents' / 'x86_64-win' / bundle.name).resolve(strict=True)
    # The native module bytes inside the bundle that the host actually loads.
    module = host_path if host_path.is_file() else next(p for p in (bundle / 'Contents').rglob('*')
                                                       if p.is_file() and p.parent.name.startswith('x86_64-'))
    load = lambda: load_plugin(str(host_path))  # noqa: E731
    print('Loading native module:', host_path, flush=True)
    host = load()

    # Bus negotiation first, before any control: a stereo-only plugin is rejected here.
    x = mono(rs.probe(PROFILE['probe'], 48000))
    try:
        y = host(x, 48000, buffer_size=rs.ACTIVE_BLOCK, reset=True)
    except ValueError as e:
        fail(f'host refused 1-in/1-out: {e}')
    if y.shape != x.shape:
        fail(f'1-channel input returned shape {y.shape}')

    neutral = rs.apply_controls(host, PROFILE['neutral'])
    neutral_state = host.raw_state
    active = rs.apply_controls(host, PROFILE['active'])
    active_all = rs.physical_values(host)
    active_state = host.raw_state
    fresh = load()
    fresh.raw_state = active_state
    recalled = rs.read_controls(fresh, PROFILE['active'])
    if rs.physical_values(fresh) != active_all:
        fail('fresh instance did not recall every active parameter')

    cases = []
    for rate in rs.RATES:
        x = mono(rs.probe(PROFILE['probe'], rate))
        noise = mono(rs.probe('alignment_noise', rate))
        # Latency may change with rate; prime once per rate so host compensation is current.
        before_prime = {}
        for label, plugin in (('host', host), ('fresh', fresh)):
            before_prime[label] = plugin.reported_latency_samples
            plugin(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
        host.raw_state = neutral_state
        rs.read_controls(host, PROFILE['neutral'])
        y0 = host(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
        latency0 = host.reported_latency_samples
        host.raw_state = active_state
        rs.read_controls(host, PROFILE['active'])
        y1 = host(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
        latency1 = host.reported_latency_samples
        z1 = fresh(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
        for label, out in (('neutral', y0), ('active', y1), ('fresh active', z1)):
            render_ok(f'{label} at {rate} Hz', out, x)
        lag = {}
        for label, state in (('neutral', neutral_state), ('active', active_state)):
            host.raw_state = state
            lag[label] = rs.alignment_lag(host(noise, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True), noise)
        if any(k or c < .5 for k, c in lag.values()) or latency0 != latency1:
            fail(f'latency alignment {lag} at {rate} Hz (reported {latency0}/{latency1})')
        effect = rs.measure(PROFILE['metric'], y1, rate) - rs.measure(PROFILE['metric'], y0, rate)
        recall = bool(np.allclose(z1, y1, atol=1e-6))
        cases.append({'rate': rate, 'block': rs.ACTIVE_BLOCK, 'input_shape': list(x.shape), 'output_shape': list(y1.shape),
                      'latency_samples': {'neutral': latency0, 'active': latency1}, 'latency_before_prime': before_prime,
                      'noise_alignment_lag_and_correlation': lag,
                      'neutral_metric': rs.measure(PROFILE['metric'], y0, rate),
                      'active_metric': rs.measure(PROFILE['metric'], y1, rate), 'effect': effect,
                      'active_peak': float(np.max(np.abs(y1))), 'fresh_instance_recall': recall,
                      'active_sha256': hashlib.sha256(y1.tobytes()).hexdigest()})
        print(f'mono {rate} Hz: shape {y1.shape} effect {effect:.2f} dB latency {latency1} recall {recall}', flush=True)
        if effect < PROFILE['min_effect']:
            fail(f"active compression effect {effect:.3f} dB < {PROFILE['min_effect']} at {rate} Hz")
        if not recall:
            fail(f'fresh instance did not recall the active audio at {rate} Hz')

    # Stereo still works through the same module after mono, and mono again after stereo.
    # Duplicated-stereo comparison checks processing only; it is not bus evidence.
    rate = 48000
    x = mono(rs.probe(PROFILE['probe'], rate))
    stereo_in = np.vstack([x, x])
    host.raw_state = active_state
    host(stereo_in, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)  # prime the stereo layout
    s1 = host(stereo_in, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
    render_ok('stereo after mono', s1, stereo_in)
    host.raw_state = neutral_state
    s0 = host(stereo_in, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
    stereo_effect = rs.measure(PROFILE['metric'], s1, rate) - rs.measure(PROFILE['metric'], s0, rate)
    if stereo_effect < PROFILE['min_effect']:
        fail(f'stereo active effect {stereo_effect:.3f} dB after mono')
    host.raw_state = active_state
    host(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
    m1 = host(x, rate, buffer_size=rs.ACTIVE_BLOCK, reset=True)
    render_ok('mono after stereo', m1, x)
    mono_vs_stereo = float(np.max(np.abs(m1[0] - s1[0])))

    receipt = {
        'scope': 'RVox VST3 hosted 1-in/1-out in Pedalboard on this OS; active compression smoke, not original '
                 'equivalence; VST3 only, not CLAP mono evidence',
        'plugin': str(host_path), 'module': str(module), 'module_sha256': hashlib.sha256(module.read_bytes()).hexdigest(),
        'os': platform.platform(), 'python': platform.python_version(), 'pedalboard': pedalboard.__version__,
        'source_commit': os.environ.get('GITHUB_SHA'),
        'bus_evidence': 'Pedalboard raises "does not support 1-channel output" for a plugin refusing the mono '
                        'layout; this module accepted (1, N) input and returned (1, N)',
        'profile': {k: PROFILE[k] for k in ('product', 'probe', 'neutral', 'active', 'metric', 'min_effect')},
        'controls': {'neutral_readback': neutral, 'active_readback': active, 'fresh_instance_readback': recalled,
                     'active_all_parameters': active_all},
        'cases': cases,
        'stereo_after_mono': {'shape': list(s1.shape), 'effect': stereo_effect},
        'mono_after_stereo_shape': list(m1.shape),
        'mono_vs_duplicated_stereo_max_abs': mono_vs_stereo,
    }
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'mono-host.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS rvox mono host smoke', flush=True)


if __name__ == '__main__':
    main()
