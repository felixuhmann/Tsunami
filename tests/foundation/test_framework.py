# SPDX-License-Identifier: GPL-3.0-or-later
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class FrameworkTests(unittest.TestCase):
    def test_release_pin_matches_download_and_agpl_selection(self):
        manifest = json.loads((ROOT / 'dependencies.lock.json').read_text())
        juce = next(d for d in manifest['dependencies'] if d['name'] == 'JUCE')
        self.assertEqual(juce['version'], '9.0.4')
        self.assertEqual(juce['commit'], '3fb5cbc0340758596dac39f7c8de47d562b4896e')
        self.assertEqual(juce['archive_sha256'], '2bb7095497a7bb7269dbdf841a7c6aa0939eaeb2a2bc9724ac4758d3ae3565a7')
        self.assertIn('AGPL', juce['license_choice'])
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn(juce['commit'], cmake)
        self.assertIn('SHA256=' + juce['archive_sha256'], cmake)

    def test_linux_runner_declares_juce9_fontconfig_and_xprop_provider(self):
        workflow = (ROOT / '.github/workflows/foundation.yml').read_text()
        self.assertIn('libfontconfig1-dev', workflow)
        self.assertIn('libxi-dev', workflow)
        self.assertIn('x11-utils', workflow)  # xprop EWMH readiness, fixed in the fetched base

    def test_native_ci_exercises_clap_with_hash_locked_validator(self):
        workflow = (ROOT / '.github/workflows/foundation.yml').read_text()
        self.assertIn('validate --json --jobs 2', workflow)
        self.assertIn('49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90', workflow)
        self.assertIn('d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10', workflow)
        self.assertIn('sha256sum -c', workflow)

    def test_local_override_rejects_old_framework_before_build(self):
        guard = ROOT / 'cmake' / 'VerifyJuce.cmake'
        self.assertTrue(guard.exists(), 'local override version guard is missing')
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder)
            (source / 'CMakeLists.txt').write_text('project(JUCE VERSION 7.0.13 LANGUAGES C CXX)\n')
            script = source / 'probe.cmake'
            script.write_text(f'include("{guard.as_posix()}")\ntsunami_verify_juce("{source.as_posix()}")\n')
            result = subprocess.run(['cmake', '-P', str(script)], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('JUCE 9.0.4 required', result.stderr)
            (source / 'CMakeLists.txt').write_text('project(JUCE VERSION 9.0.4 LANGUAGES C CXX)\n')
            result = subprocess.run(['cmake', '-P', str(script)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
