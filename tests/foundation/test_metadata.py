# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class MetadataTests(unittest.TestCase):
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
