# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'cmake' / 'TsunamiPlugins.cmake'


def resolve(root, plugin_id):
    return subprocess.run(['cmake', f'-DTSUNAMI_PLUGIN_SOURCE_ROOT={root}', f'-DTSUNAMI_PLUGIN_ID={plugin_id}',
                           '-DTSUNAMI_RESOLVE_ONLY=ON', '-P', str(SCRIPT)], capture_output=True, text=True)


class RegistrationTests(unittest.TestCase):
    def make_plugin(self, root, plugin_id, files=('parameters.json', 'Processor.hpp')):
        folder = pathlib.Path(root) / 'plugins' / plugin_id
        folder.mkdir(parents=True)
        for name in files:
            (folder / name).write_text('{}\n')
        return folder

    def test_accepts_s1_and_any_plugin_folder_with_required_files(self):
        self.assertTrue(SCRIPT.exists(), 'generic plugin registration module missing')
        for plugin_id in ('s1', 'rvox', 'deesser'):
            with self.subTest(plugin_id=plugin_id), tempfile.TemporaryDirectory() as root:
                folder = self.make_plugin(root, plugin_id)
                result = resolve(root, plugin_id)
                self.assertEqual(result.returncode, 0, result.stderr)
                # CMake reports paths with forward slashes on every platform.
                self.assertIn(f'TSUNAMI_PLUGIN_DIR={folder.as_posix()}', result.stderr + result.stdout)

    def test_windows_style_backslash_root_is_normalized_at_registration(self):
        # Windows hosts pass native roots (C:\\Users\\...); the resolver must emit CMake-style paths.
        with tempfile.TemporaryDirectory() as root:
            folder = self.make_plugin(root, 's1')
            native = pathlib.Path(root).as_posix().replace('/', '\\')
            result = resolve(native, 's1')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f'TSUNAMI_PLUGIN_DIR={folder.as_posix()}\n', result.stderr + result.stdout)
            self.assertNotIn('\\', (result.stderr + result.stdout).split('TSUNAMI_PLUGIN_DIR=')[1])

    def test_rejects_invalid_ids_and_incomplete_folders(self):
        self.assertTrue(SCRIPT.exists(), 'generic plugin registration module missing')
        with tempfile.TemporaryDirectory() as root:
            self.make_plugin(root, 'partial', files=('parameters.json',))
            for plugin_id in ('../s1', 'S1', 'missing', 'partial', 'a;b'):
                with self.subTest(plugin_id=plugin_id):
                    self.assertNotEqual(resolve(root, plugin_id).returncode, 0)

    def test_real_s1_folder_resolves(self):
        result = resolve(ROOT, 's1')
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
