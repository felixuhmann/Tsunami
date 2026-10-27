# SPDX-License-Identifier: GPL-3.0-or-later
"""GUI-gate regression tests; synthetic receipts are NOT native UI evidence."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class LinuxEditorTests(unittest.TestCase):
    def test_linux_lane_keeps_audio_and_requires_mature_editor_host(self):
        script = (ROOT / 'tests/foundation/linux_host_smoke.sh').read_text()
        self.assertIn('host_smoke.py "$1" evidence', script)
        self.assertNotIn('--capture', script)
        self.assertIn('linux_editor_capture.py', script)
        workflow = (ROOT / '.github/workflows/foundation.yml').read_text()
        self.assertIn('pluginval/releases/download/v1.0.4', workflow)
        self.assertIn('c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352', workflow)

    def load_gate(self):
        spec = importlib.util.spec_from_file_location('editor_gate', ROOT / 'tests/foundation/linux_editor_capture.py')
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_window_identity_requires_the_launched_pid_and_exact_module(self):
        gate = self.load_gate()
        properties = '_NET_WM_PID(CARDINAL) = 123\nWM_CLASS(STRING) = "pluginval", "pluginval"\n'
        gate.verify_identity(properties, 123, '/exact/fixture.so', 'map /exact/fixture.so\n')
        for pid, module, maps in ((124, '/exact/fixture.so', 'map /exact/fixture.so\n'),
                                  (123, '/exact/fixture.so', 'map /other/fixture.so\n')):
            with self.assertRaises(AssertionError):
                gate.verify_identity(properties, pid, module, maps)

    def test_validator_success_without_editor_tests_is_rejected(self):
        gate = self.load_gate()
        with self.assertRaisesRegex(AssertionError, 'Editor'):
            gate.verify_validation('Plugin name: Tsunami Infrastructure Fixture\nSUCCESS\n')
        log = 'Plugin name: Tsunami Infrastructure Fixture\n' + '\n'.join(
            'Completed tests in pluginval / ' + name for name in ('Editor', 'Open editor whilst processing', 'Editor Automation')) + '\nSUCCESS\n'
        gate.verify_validation(log)
        with self.assertRaises(AssertionError):
            gate.verify_validation(log.replace('SUCCESS', 'FAILURE'))

if __name__ == '__main__':
    unittest.main()
