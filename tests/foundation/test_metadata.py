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

    def load(self):
        spec = importlib.util.spec_from_file_location('metadata', ROOT / 'tools' / 'generate_metadata.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_logarithmic_taper_emits_geometric_midpoint_skew(self):
        text = self.load().generate({'parameters': [dict(index=0, id='frequency', name='Frequency', min=20, max=20000, default=1000, unit='Hz', logarithmic=True)]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        # skew s maps normalised .5 to sqrt(min*max): ((632.456-20)/19980)^s = .5
        self.assertRegex(text, r'"Hz", 0\.0f, 0\.19889\d*f}')

    def test_explicit_skew_taper_and_rejects_bad_log_ranges(self):
        module = self.load()
        text = module.generate({'parameters': [dict(index=0, id='time', name='Time', min=0, max=10, default=1, taper={'skew': 0.3})]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        self.assertIn('"", 0.0f, 0.3f}', text)
        for bad in (dict(min=0, max=10, default=1, logarithmic=True), dict(min=1, max=10, default=1, taper={'skew': 0})):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                module.generate({'parameters': [dict(index=0, id='x', name='X') | bad]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')

    def test_choice_parameters_have_unit_steps_and_labels(self):
        module = self.load()
        text = module.generate({'parameters': [dict(index=0, id='mode', name='Mode', min=0, max=1, default=0, type='choice', choices=['BandPass', 'HighPass'])]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        self.assertIn('"", 1.0f, 1.0f, "BandPass|HighPass"}', text)
        with self.assertRaises(ValueError):
            module.generate({'parameters': [dict(index=0, id='mode', name='Mode', min=0, max=2, default=0, type='choice', choices=['A', 'B'])]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')

    def test_display_decimals_is_explicit_text_only_precision(self):
        module = self.load()
        base = dict(index=0, id='freq', name='Freq', min=2000, max=16000, default=5506, unit='Hz', logarithmic=True)
        plain = module.generate({'parameters': [base]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        whole = module.generate({'parameters': [dict(base, display_decimals=0)]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        skew = plain.split('"Hz", 0.0f, ')[1].split('}')[0]
        # Same physical range/step/skew; only the trailing text precision is added (choices slot nullptr).
        self.assertIn(f'"Hz", 0.0f, {skew}, nullptr, 0}}', whole)
        linear = module.generate({'parameters': [dict(base, logarithmic=False, display_decimals=2)]}, 'fixture', 'Fixture',
                                 'tsunami::fixture::Processor')
        self.assertIn('"Hz", 0.0f, 1.0f, nullptr, 2}', linear)
        for bad in (-1, True, False, 0.5, 1.0, '0', None, 7):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                module.generate({'parameters': [dict(base, display_decimals=bad)]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')
        with self.assertRaises(ValueError):  # choices already define their text
            module.generate({'parameters': [dict(index=0, id='mode', name='Mode', min=0, max=1, default=0, type='choice',
                                                 choices=['A', 'B'], display_decimals=0)]}, 'fixture', 'Fixture', 'tsunami::fixture::Processor')

    def test_default_generated_bytes_unchanged(self):
        import hashlib
        module = self.load()
        s1 = module.generate(json.loads((ROOT / 'plugins/s1/parameters.json').read_text()), 's1', 'Tsunami Stereo',
                             'tsunami::s1::Processor', editor=True)
        fixture = module.generate(json.loads((ROOT / 'tests/foundation/fixture/parameters.json').read_text()), 'fixture',
                                  'Tsunami Infrastructure Fixture', 'tsunami::fixture::Processor')
        # Recorded from the generator at cb25f1e before display_decimals existed.
        self.assertEqual(hashlib.sha256(s1.encode()).hexdigest(), '261f03dfe349a1f45b8eb1172a56758e0e5df4d0a284289a0df354b9d20ed82f')
        self.assertEqual(hashlib.sha256(fixture.encode()).hexdigest(), '58d21e1afd3a95ec0318d35152d6db3cfc3be35c19e3c8a06fbb02fe170ac62e')

if __name__ == '__main__':
    unittest.main()
