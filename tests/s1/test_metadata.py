# SPDX-License-Identifier: GPL-3.0-or-later
"""Portable-core metadata is the adapter's executable control contract."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_metadata import generate

class MetadataTest(unittest.TestCase):
    def test_physical_controls_match_processor_order_and_ranges(self):
        path = ROOT / 'plugins/s1/parameters.json'
        self.assertTrue(path.exists(), 'S1 core must provide adapter metadata')
        rows = json.loads(path.read_text())['parameters']
        expected = [('gain', -24, 0, 0), ('width', 0, 3, 1),
                    ('rotation', -45, 45, 0), ('asymmetry', -90, 90, 0),
                    ('swap', 0, 1, 0), ('invert_left', 0, 1, 0),
                    ('invert_right', 0, 1, 0), ('input_mode', 0, 1, 0),
                    ('bypass', 0, 1, 0)]
        self.assertEqual([(r['id'], r['min'], r['max'], r['default']) for r in rows], expected)
        self.assertEqual([r['index'] for r in rows], list(range(9)))
        self.assertTrue(all(r['type'] == 'bool' for r in rows[4:]))
        self.assertTrue(all(r['group'] for r in rows))
        header = generate({'parameters': rows}, 's1', 'Tsunami Stereo', 'tsunami::s1::Processor')
        self.assertIn('using Core = tsunami::s1::Processor;', header)
        self.assertEqual(header.count('1.0f}'), 5)

if __name__ == '__main__':
    unittest.main()
