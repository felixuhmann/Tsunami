# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-file tests for cmake/derive_source.py (hash-bound build-local source derivation)."""
import hashlib, json, os, subprocess, sys, tempfile, time, unittest

TOOL = os.path.join(os.path.dirname(__file__), "..", "..", "cmake", "derive_source.py")
SRC = b"int a = 1;\nint b = 2;\n"
OUT = b"int a = 1;\nint b = 3;\n"
EDITS = [{"find": ["int b = 2;"], "replace": ["int b = 3;"]}]


def sha(b):
    return hashlib.sha256(b).hexdigest()


class DeriveSource(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.dir.name, "in.cpp")
        self.edits = os.path.join(self.dir.name, "edits.json")
        self.out = os.path.join(self.dir.name, "sub", "out.cpp")
        self.write(self.src, SRC)
        self.write(self.edits, json.dumps(EDITS).encode())

    def tearDown(self):
        self.dir.cleanup()

    def write(self, path, data):
        with open(path, "wb") as f:
            f.write(data)

    def run_tool(self, src_hash=sha(SRC), out_hash=sha(OUT)):
        return subprocess.run([sys.executable, TOOL, "--input", self.src, "--input-sha256", src_hash,
                               "--edits", self.edits, "--output", self.out, "--output-sha256", out_hash],
                              capture_output=True, text=True)

    def test_derives_exact_output(self):
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(self.out, "rb") as f:
            self.assertEqual(f.read(), OUT)
        with open(self.src, "rb") as f:
            self.assertEqual(f.read(), SRC)  # source never mutated

    def test_rejects_source_drift(self):
        self.write(self.src, SRC + b"// drift\n")
        r = self.run_tool()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("input sha256 mismatch", r.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_labels_and_normalizes_crlf_checkout(self):
        # Windows git checkouts may convert to CRLF; hashes bind the LF-normalized text.
        self.write(self.src, SRC.replace(b"\n", b"\r\n"))
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("CRLF input normalized to LF", r.stdout)
        with open(self.out, "rb") as f:
            self.assertEqual(f.read(), OUT)
        self.write(self.src, SRC.replace(b"\n", b"\r\n") + b"// drift\r\n")
        r = self.run_tool()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("input sha256 mismatch", r.stderr)
        self.assertIn("CRLF", r.stderr)

    def test_rejects_edit_drift(self):
        for edits in ([{"find": ["int c = 2;"], "replace": ["x"]}],
                      [{"find": ["int "], "replace": ["x"]}]):  # anchor must match exactly once
            self.write(self.edits, json.dumps(edits).encode())
            r = self.run_tool()
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("must match exactly once", r.stderr)
            self.assertFalse(os.path.exists(self.out))

    def test_rejects_output_hash_mismatch(self):
        r = self.run_tool(out_hash=sha(b"other"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("output sha256 mismatch", r.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_rejects_malformed_edits(self):
        self.write(self.edits, json.dumps([{"find": "int b = 2;"}]).encode())
        r = self.run_tool()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("malformed edits", r.stderr)

    def test_no_rewrite_when_unchanged(self):
        self.assertEqual(self.run_tool().returncode, 0)
        old = time.time() - 1000
        os.utime(self.out, (old, old))
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(os.stat(self.out).st_mtime, old)

    def test_replaces_stale_output(self):
        os.makedirs(os.path.dirname(self.out))
        self.write(self.out, b"stale")
        self.assertEqual(self.run_tool().returncode, 0)
        with open(self.out, "rb") as f:
            self.assertEqual(f.read(), OUT)


if __name__ == "__main__":
    unittest.main()
