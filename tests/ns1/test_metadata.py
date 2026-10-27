# SPDX-License-Identifier: GPL-3.0-or-later
import json, os, re, unittest
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
class Metadata(unittest.TestCase):
    def test_parameters_match_processor_indices(self):
        d = json.load(open(os.path.join(ROOT, "plugins/ns1/parameters.json")))
        src = open(os.path.join(ROOT, "plugins/ns1/Processor.hpp")).read()
        p = {x["id"]: x for x in d["parameters"]}
        self.assertEqual([x["index"] for x in d["parameters"]], [0, 1])
        self.assertEqual((p["ns1"]["min"], p["ns1"]["max"], p["ns1"]["default"], p["ns1"]["unit"]), (0, 100, 0, "%"))
        self.assertEqual((p["bypass"]["type"], p["bypass"]["default"]), ("bool", 0))
        self.assertRegex(src, r"Amount\s*=\s*0")
        self.assertRegex(src, r"Bypass\s*=\s*1")
if __name__ == "__main__":
    unittest.main()
