# SPDX-License-Identifier: GPL-3.0-or-later
"""Regressions for the downloaded-package active-processing smoke (tools/release_smoke.py).

Profile selection is pure and runs everywhere. The end-to-end cases load actual
extracted release packages (output of `release.py verify`) in Pedalboard; point
TSUNAMI_SMOKE_PACKAGES at a directory holding `<id>/<package>/MANIFEST.json`.
These are package/processing smoke checks, NOT original-equivalence tests.
"""
import copy
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import release_smoke  # noqa: E402

PACKAGES = Path(os.environ['TSUNAMI_SMOKE_PACKAGES']) if os.environ.get('TSUNAMI_SMOKE_PACKAGES') else None
HOSTED = PACKAGES is not None and all(importlib.util.find_spec(m) for m in ('numpy', 'pedalboard'))


def package(plugin_id):
    found = sorted((PACKAGES / plugin_id).glob('*/MANIFEST.json'))
    if len(found) != 1:
        raise unittest.SkipTest(f'no single extracted {plugin_id} package under {PACKAGES}')
    return found[0].parent


class ProfileSelection(unittest.TestCase):
    def test_unknown_target_fails_closed(self):
        with self.assertRaises(SystemExit) as e:
            release_smoke.active_profile({'id': 'rcomp', 'product': 'Tsunami Compressor'})
        self.assertIn('unsupported-profile', str(e.exception))
        self.assertIn('rcomp', str(e.exception))

    def test_known_id_with_other_product_does_not_inherit_profile(self):
        with self.assertRaises(SystemExit) as e:
            release_smoke.active_profile({'id': 's1', 'product': 'Tsunami De-Esser'})
        self.assertIn('unsupported-profile', str(e.exception))

    def test_profiles_are_product_qualified(self):
        for plugin_id, product in (('s1', 'Tsunami Stereo'), ('deesser', 'Tsunami De-Esser'), ('rvox', 'Tsunami Voice')):
            profile = release_smoke.active_profile({'id': plugin_id, 'product': product})
            self.assertEqual(profile['product'], product)
            self.assertNotEqual(profile['neutral'], profile['active'])


@unittest.skipUnless(HOSTED, 'set TSUNAMI_SMOKE_PACKAGES and install tools/host-requirements.txt')
class DownloadedPackageActiveProcessing(unittest.TestCase):
    def smoke(self, plugin_id):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'receipt.json'
            run = subprocess.run([sys.executable, str(ROOT / 'tools' / 'release_smoke.py'), str(package(plugin_id)), str(out)],
                                 capture_output=True, text=True, timeout=600)
            self.assertEqual(run.returncode, 0, run.stderr[-2000:])
            return json.loads(out.read_text())

    def assert_active(self, receipt, plugin_id, product, controls):
        active = receipt['active_processing']
        self.assertEqual((receipt['id'], receipt['product'], active['profile']), (plugin_id, product, plugin_id))
        self.assertIn('not original-equivalence', active['scope'])
        for state in ('neutral', 'active'):
            self.assertEqual(set(active['controls'][state]['requested']), set(controls[state]))
            for name, value in controls[state].items():
                self.assertAlmostEqual(active['controls'][state]['readback'][name], value, places=3)
        self.assertTrue(active['cases'])
        for case in active['cases']:
            self.assertGreaterEqual(case['effect'], active['min_effect'])
            self.assertTrue(case['fresh_instance_recall'])
            self.assertNotEqual(case['neutral_sha256'], case['active_sha256'])
            self.assertGreater(case['active_peak'], 1e-3)
        return active

    def test_s1_width_expands_side_of_asymmetric_stereo_probe(self):
        active = self.assert_active(self.smoke('s1'), 's1', 'Tsunami Stereo',
                                    {'neutral': {'width': 1.0}, 'active': {'width': 2.4}})
        self.assertEqual(active['metric'], 'side_to_mid_db')

    def test_deesser_threshold_reduces_high_band_and_keeps_low_band(self):
        active = self.assert_active(self.smoke('deesser'), 'deesser', 'Tsunami De-Esser',
                                    {'neutral': {'threshold_db': 0.0}, 'active': {'threshold_db': -36.0}})
        self.assertEqual(active['metric'], 'high_band_reduction_db')
        for case in active['cases']:
            self.assertLessEqual(abs(case['guard']['low_band_change_db']), active['guard_max_db'])

    def test_rvox_compression_reduces_latency_aligned_dynamic_range(self):
        fixed = {'gate_db': -80.0, 'gain_db': 0.0}
        active = self.assert_active(self.smoke('rvox'), 'rvox', 'Tsunami Voice',
                                    {'neutral': {'compression_db': 0.0, **fixed},
                                     'active': {'compression_db': -18.0, **fixed}})
        self.assertEqual(active['metric'], 'dynamic_range_reduction_db')
        for case in active['cases']:
            self.assertEqual(case['latency_samples']['neutral'], case['latency_samples']['active'])
            self.assertGreater(case['latency_samples']['active'], 0)


@unittest.skipUnless(HOSTED, 'set TSUNAMI_SMOKE_PACKAGES and install tools/host-requirements.txt')
class ActiveGateRejectsInactiveControls(unittest.TestCase):
    """The gate must reject a profile whose 'active' setting does not actually process."""

    def gate(self, plugin_id, **override):
        from pedalboard import load_plugin
        root = package(plugin_id)
        manifest = json.loads((root / 'MANIFEST.json').read_text())
        profile = {**copy.deepcopy(release_smoke.active_profile(manifest)), **override}
        bundle = str(root / manifest['formats']['vst3'])
        with self.assertRaises(SystemExit) as e:
            release_smoke.check_active(lambda: load_plugin(bundle), manifest, profile)
        return str(e.exception)

    def test_neutral_values_as_active_are_rejected_for_every_profile(self):
        for plugin_id in ('s1', 'deesser', 'rvox'):
            neutral = release_smoke.PROFILES[plugin_id]['neutral']
            with self.subTest(plugin_id):
                self.assertIn('active-processing gate', self.gate(plugin_id, active=dict(neutral)))

    def test_wrong_deesser_control_without_threshold_is_rejected(self):
        # Moving only the detector frequency leaves threshold at the neutral 0 dB: no reduction.
        message = self.gate('deesser', neutral={'threshold_db': 0.0, 'freq_hz': 2000.0},
                            active={'threshold_db': 0.0, 'freq_hz': 16000.0})
        self.assertIn('active-processing gate', message)

    def test_control_absent_from_plugin_is_not_guessed(self):
        message = self.gate('s1', neutral={'compression_db': 0.0}, active={'compression_db': -18.0})
        self.assertIn("'compression_db' not exposed", message)


if __name__ == '__main__':
    unittest.main()
