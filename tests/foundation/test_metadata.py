# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import pathlib
import unittest
import json
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

class MetadataTests(unittest.TestCase):
    def test_cli_selects_only_a_plugin_local_editor(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)
            metadata = source / 'parameters.json'
            metadata.write_text(json.dumps({'parameters': [dict(index=0, id='gain', name='Gain', min=0, max=2, default=1)]}))
            output = source / 'Generated.hpp'
            command = [sys.executable, str(ROOT / 'tools/generate_metadata.py'), str(metadata), str(output),
                       '--id', 'fixture', '--product', 'Fixture', '--core', 'tsunami::fixture::Processor']
            subprocess.run(command, check=True)
            self.assertNotIn('#include "Editor.hpp"', output.read_text())
            (source / 'Editor.hpp').write_text('// plugin-local header\n')
            subprocess.run(command, check=True)
            text = output.read_text()
            self.assertIn('#include "Editor.hpp"', text)
            self.assertIn('return tsunami::createPluginEditor(processor, state);', text)

    def test_emits_physical_parameter_descriptor(self):
        path = ROOT / 'tools' / 'generate_metadata.py'
        self.assertTrue(path.exists(), 'metadata generator is not implemented')
        spec = importlib.util.spec_from_file_location('metadata', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        text = module.generate({'parameters': [dict(index=0, id='gain', name='Gain', min=-24, max=0, default=0, unit='dB', type='float')]}, 'fixture', 'Infrastructure Fixture', 'tsunami::fixture::Processor')
        self.assertIn('"gain", "Gain", -24.0f, 0.0f, 0.0f, "dB"', text)
        self.assertIn('using Core = tsunami::fixture::Processor;', text)

    def test_rejects_invalid_physical_ranges_and_indices(self):
        spec = importlib.util.spec_from_file_location('metadata', ROOT / 'tools' / 'generate_metadata.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        valid = dict(index=0, id='gain', name='Gain', min=-24, max=0, default=0)
        for patch in ({'min': 1}, {'default': 2}, {'max': float('inf')}, {'index': 2}, {'id': '../bad'}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                module.generate({'parameters': [valid | patch]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')

    def test_discrete_controls_have_unit_steps(self):
        spec = importlib.util.spec_from_file_location('metadata', ROOT / 'tools' / 'generate_metadata.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        text = module.generate({'parameters': [dict(index=0, id='bypass', name='Bypass', min=0, max=1, default=0, type='bool')]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        self.assertIn('"", 1.0f}', text)

    def test_rejects_unsupported_log_mapping(self):
        spec = importlib.util.spec_from_file_location('metadata', ROOT / 'tools' / 'generate_metadata.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with self.assertRaisesRegex(ValueError, 'logarithmic'):
            module.generate({'parameters': [dict(index=0, id='frequency', name='Frequency', min=20, max=20000, default=1000, logarithmic=True)]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')

if __name__ == '__main__':
    unittest.main()
