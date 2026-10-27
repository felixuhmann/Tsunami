#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gate regression: the RVox native workflow must host the built VST3 with 1-in/1-out
input on BOTH Windows and Linux (tests/rvox/mono_host_smoke.py), not only stereo.

Reads .github/workflows/rvox.yml as text (no YAML dependency on runners)."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github' / 'workflows' / 'rvox.yml'
SMOKE = ROOT / 'tests' / 'rvox' / 'mono_host_smoke.py'


def steps(text):
    """(name, body) for each job step, in order."""
    parts = re.split(r'(?m)^      - ', text.split('\n    steps:\n', 1)[1])
    out = []
    for part in parts[1:]:
        m = re.search(r'(?m)^(?:        )?name:\s*(.*)', part)
        out.append((m.group(1).strip() if m else '', part))
    return out


class NativeMonoGate(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text(encoding='utf-8')
        self.steps = steps(self.text)

    def test_matrix_covers_windows_and_linux(self):
        matrix = re.search(r'os:\s*\[([^\]]*)\]', self.text)
        self.assertTrue(matrix and 'ubuntu-' in matrix.group(1) and 'windows-' in matrix.group(1))

    def test_mono_smoke_runs_on_both_platforms(self):
        mono = [(i, n, b) for i, (n, b) in enumerate(self.steps) if 'tests/rvox/mono_host_smoke.py' in b]
        self.assertEqual(len(mono), 1, 'exactly one workflow step must run tests/rvox/mono_host_smoke.py')
        index, name, body = mono[0]
        self.assertNotRegex(body, r'(?m)^(?:        )?if:', f'mono host step {name!r} must not be platform-gated')
        self.assertNotRegex(body, r'continue-on-error', f'mono host step {name!r} must be able to fail the job')
        # Shell-level OS branches must run the smoke on both sides.
        if 'RUNNER_OS' in body:
            self.assertEqual(body.count('tests/rvox/mono_host_smoke.py'), 2, 'mono smoke must run on both OS branches')
            self.assertIn('else', body)
        self.assertIn('Tsunami Voice.vst3', body, 'mono step must host the built RVox VST3 bundle')
        self.assertIn('evidence', body, 'mono receipt must land in the uploaded evidence directory')
        wheels = [i for i, (n, b) in enumerate(self.steps) if 'tools/host-requirements.txt' in b]
        self.assertTrue(wheels and wheels[0] < index, 'mono step must run after the pinned host wheels install')
        builds = [i for i, (n, b) in enumerate(self.steps) if 'TsunamiRVox_VST3' in b]
        self.assertTrue(builds and builds[0] < index, 'mono step must run after the VST3 build')

    def test_mono_smoke_reuses_reviewed_rvox_profile(self):
        self.assertTrue(SMOKE.is_file(), 'tests/rvox/mono_host_smoke.py is missing')
        source = SMOKE.read_text(encoding='utf-8')
        self.assertIn('release_smoke', source, 'mono smoke must reuse the reviewed release-smoke RVox profile')
        self.assertIn("PROFILES['rvox']", source)

    def test_clap_abi_harness_built_and_required_on_both_platforms(self):
        builds = [b for n, b in self.steps if 'TsunamiRVox_CLAP' in b and 'cmake --build' in b]
        self.assertEqual(len(builds), 1)
        self.assertIn('rvox_clap_abi_test', builds[0], 'the RVox CLAP ABI harness must be built')
        ctest = [b for n, b in self.steps if 'ctest --test-dir build' in b]
        self.assertTrue(ctest, 'no CTest step')
        for body in ctest:
            self.assertNotRegex(body, r'(?m)^(?:        )?if:')
            self.assertNotRegex(body, r'continue-on-error')
        runs = [line for b in ctest for line in b.splitlines() if 'ctest --test-dir build' in line and ' -N' not in line]
        self.assertEqual(len(runs), 2, 'CTest must run on both OS branches')
        for line in runs:
            self.assertIn('clap_abi', line)
            self.assertIn('--no-tests=error', line)
        listed = [b for b in ctest if re.search(r"ctest .*-N .*rvox\\\.clap_abi", b)]
        self.assertTrue(listed, 'CI must fail when rvox.clap_abi is not registered')

    def test_workflow_runs_this_gate_separately(self):
        # The guard runs in its own step, so deleting the mono step alone fails CI.
        guard = [n for n, b in self.steps if 'tests/rvox/test_native_workflow.py' in b and 'mono_host_smoke.py' not in b]
        self.assertEqual(len(guard), 1, 'run tests/rvox/test_native_workflow.py in its own step')


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False, verbosity=2).result.wasSuccessful() else 1)
