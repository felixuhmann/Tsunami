# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest


class NativePathTests(unittest.TestCase):
    def resolver(self):
        path = Path(__file__).with_name('native_paths.py')
        self.assertTrue(path.is_file(), 'native bundle/module resolver is missing')
        spec = importlib.util.spec_from_file_location('native_paths', path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.resolve_native_module

    def test_linux_bundle_selects_exact_packaged_module(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory) / 'Tsunami Stereo.vst3'
            module = bundle / 'Contents/x86_64-linux' / 'Tsunami Stereo.so'
            module.parent.mkdir(parents=True)
            module.write_bytes(b'synthetic module path fixture, not executable')
            self.assertEqual(self.resolver()(bundle, 'Linux'), module.resolve())

    def test_unsupported_system_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / 'module.vst3'
            module.write_bytes(b'synthetic')
            with self.assertRaises(ValueError):
                self.resolver()(module, 'Darwin')

    def test_windows_bundle_selects_exact_packaged_module(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory) / 'Tsunami Stereo.vst3'
            module = bundle / 'Contents/x86_64-win' / bundle.name
            module.parent.mkdir(parents=True)
            module.write_bytes(b'synthetic module path fixture, not executable')
            self.assertEqual(self.resolver()(bundle, 'Windows'), module.resolve())


if __name__ == '__main__':
    unittest.main()
