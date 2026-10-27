#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release-package smoke: load a verified, extracted package's VST3 in Pedalboard.

Checks module hash against MANIFEST.json before loading, then finite non-silent
processing across rates/blocks, a state round trip into a fresh instance and
deterministic output. A product-qualified active-processing profile then sets and
reads back physical control values and requires a measured effect of the active
setting over an explicit neutral setting through the same plugin, plus recall of
the active state and audio in a fresh instance. Targets without a profile fail
closed. This is packaging evidence, NOT original-equivalence evidence; CLAP is
packaged but not hosted here.
"""
import argparse
import hashlib
import json
import platform
import sys
from pathlib import Path

RATES = (44100, 48000, 96000)
BLOCKS = (1, 64, 480, 1024)
ACTIVE_BLOCK = 256
ACTIVE_SECONDS = .75
# Leading samples excluded from metrics while detectors settle.
SETTLE_SECONDS = .15
# Pedalboard compensates the host-reported latency; the measured output/input lag must be 0.
MAX_ALIGNMENT_LAG = 512

# Product-qualified active-processing profiles. Each names physical control values
# (host units) for a neutral negative and an active positive, the deterministic probe
# and the measured effect the active setting must add over neutral. Smoke evidence
# that the package processes as configured; NOT original-equivalence evidence.
PROFILES = {
    's1': {
        'product': 'Tsunami Stereo',
        'probe': 'asymmetric_stereo',
        'neutral': {'width': 1.0},
        'active': {'width': 2.4},
        'metric': 'side_to_mid_db',
        'min_effect': 3.0,
        'signal_path': 'L/R -> M/S matrix, side scaled by width -> L/R; '
                       'effect = output side/mid level ratio (dB), active minus neutral',
    },
    'deesser': {
        'product': 'Tsunami De-Esser',
        'probe': 'low_high_tones',
        'neutral': {'threshold_db': 0.0},
        'active': {'threshold_db': -36.0},
        'metric': 'high_band_reduction_db',
        'min_effect': 3.0,
        'guard': 'low_band_change_db',
        'guard_max_db': 1.5,
        'signal_path': 'high-passed sidechain detector vs threshold -> gain reduction of the split high band; '
                       'effect = 7 kHz band level drop (dB) active vs neutral; guard = 300 Hz band level change',
    },
    'rvox': {
        'product': 'Tsunami Voice',
        'probe': 'level_steps',
        # Gate held at its -80 dB floor (disabled) and output gain at unity in both states.
        'neutral': {'compression_db': 0.0, 'gate_db': -80.0, 'gain_db': 0.0},
        'active': {'compression_db': -18.0, 'gate_db': -80.0, 'gain_db': 0.0},
        'metric': 'dynamic_range_reduction_db',
        'min_effect': 3.0,
        'signal_path': 'lookahead level detector -> compression gain computer -> delayed audio x gain; '
                       'effect = loud-minus-quiet segment level (dB) of the latency-aligned output, '
                       'neutral minus active',
    },
}


def active_profile(manifest):
    """Return the profile for exactly this target id AND product, else fail closed."""
    profile = PROFILES.get(manifest.get('id'))
    if profile is None or profile['product'] != manifest.get('product'):
        raise SystemExit(f"unsupported-profile: no active-processing smoke profile for target "
                         f"{manifest.get('id')!r} / product {manifest.get('product')!r}; "
                         f"refusing to inherit another product's controls")
    return profile


def probe(kind, rate):
    import numpy as np
    n = int(ACTIVE_SECONDS * rate)
    t = np.arange(n) / rate
    if kind == 'asymmetric_stereo':
        # Different frequencies, levels, phase and a little noise per side: genuine stereo.
        noise = np.random.default_rng(7).standard_normal(n)
        x = np.stack([.3 * np.sin(2 * np.pi * 440 * t), .2 * np.sin(2 * np.pi * 660 * t + .7) + .02 * noise])
    elif kind == 'low_high_tones':
        # A 300 Hz body tone plus a 7 kHz sibilance-band tone, identical in both channels.
        tone = .3 * np.sin(2 * np.pi * 300 * t) + .3 * np.sin(2 * np.pi * 7000 * t)
        x = np.stack([tone, tone])
    elif kind == 'level_steps':
        # 220 Hz tone alternating 0.5 / 0.05 amplitude every 125 ms, identical in both channels.
        tone = level_envelope(t) * np.sin(2 * np.pi * 220 * t)
        x = np.stack([tone, tone])
    elif kind == 'alignment_noise':
        # Aperiodic, so the output/input cross-correlation has a single unambiguous peak.
        x = .1 * np.random.default_rng(11).standard_normal((2, n))
    else:
        raise SystemExit(f'unsupported-profile: unknown probe {kind!r}')
    return x.astype(np.float32)


def level_envelope(t):
    import numpy as np
    return np.where((t * 4) % 1 < .5, .5, .05)


def _db(x):
    import numpy as np
    return float(20 * np.log10(np.sqrt(np.mean(np.asarray(x, np.float64) ** 2)) + 1e-30))


def _band_db(a, rate, hz):
    import numpy as np
    mono = a.mean(axis=0)
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(mono.size)))
    freqs = np.fft.rfftfreq(mono.size, 1 / rate)
    band = (freqs > .9 * hz) & (freqs < 1.1 * hz)
    return float(10 * np.log10(np.sum(spectrum[band] ** 2) + 1e-30))


def alignment_lag(y, x):
    """Lag (samples) of output y behind input x maximizing their mono cross-correlation,
    and the normalized correlation at that lag (a weak peak means no reliable alignment)."""
    import numpy as np
    a, b = y.astype(np.float64).mean(axis=0), x.astype(np.float64).mean(axis=0)
    ref = b[MAX_ALIGNMENT_LAG:a.size - MAX_ALIGNMENT_LAG]

    def corr(k):
        seg = a[MAX_ALIGNMENT_LAG + k:a.size - MAX_ALIGNMENT_LAG + k]
        return float(np.dot(seg, ref) / (np.linalg.norm(seg) * np.linalg.norm(ref) + 1e-30))
    lag = max(range(-MAX_ALIGNMENT_LAG, MAX_ALIGNMENT_LAG + 1), key=corr)
    return lag, corr(lag)


def measure(metric, y, rate):
    """Metric of latency-compensated output y past the settle window.

    Oriented so that a larger value means more of the profile's intended processing."""
    a = y[:, int(SETTLE_SECONDS * rate):].astype('float64')
    if metric == 'side_to_mid_db':
        return _db((a[0] - a[1]) / 2) - _db((a[0] + a[1]) / 2)
    if metric == 'high_band_reduction_db':
        return -_band_db(a, rate, 7000)
    if metric == 'low_band_change_db':
        return _band_db(a, rate, 300)
    if metric == 'dynamic_range_reduction_db':
        import numpy as np
        # Latency-compensated output sample settle + k carries input sample settle + k.
        settle = int(SETTLE_SECONDS * rate)
        loud = level_envelope(np.arange(settle, settle + a.shape[1]) / rate) > .3
        return -(_db(a[:, loud]) - _db(a[:, ~loud]))
    raise SystemExit(f'unsupported-profile: unknown metric {metric!r}')


def apply_controls(host, controls):
    readback = {}
    for name, value in controls.items():
        if name not in host.parameters:
            raise SystemExit(f'profile control {name!r} not exposed by the plugin; refusing to guess a mapping')
        setattr(host, name, value)
        readback[name] = float(getattr(host, name))
        if abs(readback[name] - value) > 1e-3:
            raise SystemExit(f'control {name!r} read back {readback[name]} after setting {value}')
    return readback


def read_controls(host, controls):
    """Read back profile controls; fail if the host does not hold the requested values."""
    values = {k: float(getattr(host, k)) for k in controls}
    for name, value in controls.items():
        if abs(values[name] - value) > 1e-3:
            raise SystemExit(f'control {name!r} reads {values[name]} at render time, expected {value}')
    return values


def physical_values(host):
    # Host display string plus normalized value: valid for float, bool and choice parameters alike.
    return {k: {'display': v.string_value, 'raw': float(v.raw_value)} for k, v in host.parameters.items()}


def check_active(load, manifest, profile):
    """Neutral negative vs active positive through the same plugin; fresh-instance recall."""
    import numpy as np
    host = load()
    neutral = apply_controls(host, profile['neutral'])
    neutral_all = physical_values(host)
    neutral_state = host.raw_state
    active = apply_controls(host, profile['active'])
    active_all = physical_values(host)
    active_state = host.raw_state
    fresh = load()
    fresh.raw_state = active_state
    recalled = read_controls(fresh, profile['active'])
    if physical_values(fresh) != active_all:
        raise SystemExit('fresh instance did not recall every active parameter')
    cases = []
    for rate in RATES:
        x = probe(profile['probe'], rate)
        # A plugin may report a rate-dependent latency that the host only picks up after the
        # first render at a new rate; render once at this rate so compensation is current.
        latency_before_prime = {}
        for label, plugin in (('host', host), ('fresh', fresh)):
            latency_before_prime[label] = plugin.reported_latency_samples
            plugin(x, rate, buffer_size=ACTIVE_BLOCK, reset=True)
        host.raw_state = neutral_state
        read_controls(host, profile['neutral'])
        y0 = host(x, rate, buffer_size=ACTIVE_BLOCK, reset=True)
        latency0 = host.reported_latency_samples
        host.raw_state = active_state
        read_controls(host, profile['active'])
        y1 = host(x, rate, buffer_size=ACTIVE_BLOCK, reset=True)
        latency1 = host.reported_latency_samples
        z1 = fresh(x, rate, buffer_size=ACTIVE_BLOCK, reset=True)
        for label, y in (('neutral', y0), ('active', y1), ('fresh active', z1)):
            if y.shape != x.shape or not np.isfinite(y).all() or np.max(np.abs(y)) <= 1e-3:
                raise SystemExit(f'{label} output trivial or non-finite at {rate} Hz')
        noise = probe('alignment_noise', rate)
        lag = {}
        for label, state in (('neutral', neutral_state), ('active', active_state)):
            host.raw_state = state
            lag[label] = alignment_lag(host(noise, rate, buffer_size=ACTIVE_BLOCK, reset=True), noise)
        if any(k or c < .5 for k, c in lag.values()) or latency0 != latency1:
            raise SystemExit(f'latency alignment: (lag samples, correlation) {lag} at {rate} Hz '
                             f'(reported latency neutral {latency0}, active {latency1})')
        effect = measure(profile['metric'], y1, rate) - measure(profile['metric'], y0, rate)
        guard = None
        if 'guard' in profile:
            guard = {profile['guard']: measure(profile['guard'], y1, rate) - measure(profile['guard'], y0, rate)}
            if abs(guard[profile['guard']]) > profile['guard_max_db']:
                raise SystemExit(f"active-processing guard: {profile['guard']} {guard[profile['guard']]:.3f} "
                                 f"exceeds {profile['guard_max_db']} at {rate} Hz")
        recall = bool(np.allclose(z1, y1, atol=1e-6)) and all(abs(recalled[k] - v) <= 1e-3 for k, v in active.items())
        cases.append({'rate': rate, 'block': ACTIVE_BLOCK, 'samples': x.shape[1],
                      'probe_sha256': hashlib.sha256(x.tobytes()).hexdigest(),
                      'latency_samples': {'neutral': latency0, 'active': latency1},
                      'latency_before_prime': latency_before_prime, 'noise_alignment_lag_and_correlation': lag,
                      'neutral_metric': measure(profile['metric'], y0, rate),
                      'active_metric': measure(profile['metric'], y1, rate), 'effect': effect, 'guard': guard,
                      'neutral_peak': float(np.max(np.abs(y0))), 'active_peak': float(np.max(np.abs(y1))),
                      'neutral_sha256': hashlib.sha256(y0.tobytes()).hexdigest(),
                      'active_sha256': hashlib.sha256(y1.tobytes()).hexdigest(),
                      'fresh_instance_recall': recall})
        if effect < profile['min_effect']:
            raise SystemExit(f"active-processing gate: {profile['metric']} effect {effect:.3f} < "
                             f"{profile['min_effect']} at {rate} Hz (active {profile['active']} vs neutral {profile['neutral']})")
        if not recall:
            raise SystemExit(f'fresh instance did not recall the active state/audio at {rate} Hz')
    return {
        'scope': 'active-processing smoke: active vs neutral controls through the same plugin; not original-equivalence',
        'profile': manifest['id'], 'product': profile['product'], 'probe': profile['probe'],
        'signal_path': profile['signal_path'], 'metric': profile['metric'], 'min_effect': profile['min_effect'],
        'guard': profile.get('guard'), 'guard_max_db': profile.get('guard_max_db'),
        'latency_alignment': 'host compensates reported latency after one priming render per rate; '
                             'neutral- and active-state noise-probe output/input cross-correlation lag must be 0 '
                             '(normalized peak >= 0.5) and reported latency equal; metrics skip a settle '
                             'window and compare neutral vs active outputs of the same plugin, not the dry input',
        'controls': {'neutral': {'requested': profile['neutral'], 'readback': neutral, 'all_parameters': neutral_all},
                     'active': {'requested': profile['active'], 'readback': active, 'all_parameters': active_all,
                                'fresh_instance_readback': recalled}},
        'cases': cases,
    }


def main():
    import numpy as np
    import pedalboard
    from pedalboard import load_plugin
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='extracted package directory (output of release.py verify)')
    parser.add_argument('receipt', type=Path)
    a = parser.parse_args()
    root = a.package.resolve(strict=True)
    manifest = json.loads((root / 'MANIFEST.json').read_text(encoding='utf-8'))
    module = (root / manifest['vst3_module']).resolve(strict=True)
    bundle = (root / manifest['formats']['vst3']).resolve(strict=True)
    product = manifest['product']
    profile = active_profile(manifest)
    if manifest['formats']['vst3'] != f'VST3/{product}.vst3' or root / 'VST3' not in bundle.parents \
            or bundle not in module.parents or not bundle.is_dir() or not module.is_file():
        raise SystemExit('manifest VST3 bundle/module escapes the verified package; refusing to load')
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
    active = check_active(lambda: load_plugin(str(host_path)), manifest, profile)
    receipt = {
        'scope': 'release-package smoke of the downloaded package; NOT original-equivalence or fidelity evidence',
        'package': root.name, 'id': manifest['id'], 'product': manifest['product'], 'platform': manifest['platform'],
        'version': manifest['version'], 'commit': manifest['commit'], 'host_os': platform.platform(),
        'python': platform.python_version(), 'pedalboard': pedalboard.__version__,
        'vst3_module_sha256': module_sha, 'clap_sha256': manifest['clap_sha256'], 'clap_hosted': False,
        'parameters': list(host.parameters), 'state_parameter': name, 'state_roundtrip': True,
        'latency_samples': host.reported_latency_samples, 'cases': cases,
        'silence_peak': float(np.max(np.abs(silent))), 'active_processing': active,
    }
    a.receipt.parent.mkdir(parents=True, exist_ok=True)
    a.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({k: receipt[k] for k in ('package', 'commit', 'vst3_module_sha256', 'state_roundtrip')}, indent=2))
    print(f'{len(cases)} rate/block cases passed')
    print(f"active-processing profile {active['profile']}: min {active['metric']} effect "
          f"{min(c['effect'] for c in active['cases']):.3f} >= {active['min_effect']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
