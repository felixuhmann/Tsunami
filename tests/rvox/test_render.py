#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest


class RendererValidation(unittest.TestCase):
    def test_rejects_partial_stereo_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)/'input.raw'
            dest = pathlib.Path(directory)/'output.raw'
            source.write_bytes(struct.pack('<3f', .1, .2, .3))
            result = subprocess.run([RENDER,str(source),str(dest),'2','48000','0','-80','0'],capture_output=True)
            self.assertNotEqual(result.returncode,0, 'partial stereo frames must not be silently discarded')
            self.assertFalse(dest.exists())

    def test_rejects_nonfinite_control(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)/'input.raw'
            dest = pathlib.Path(directory)/'output.raw'
            source.write_bytes(struct.pack('<f', .1))
            result = subprocess.run([RENDER,str(source),str(dest),'1','48000','nan','-80','0'],capture_output=True)
            self.assertNotEqual(result.returncode,0, 'NaN must not silently render at the default setting')
            self.assertFalse(dest.exists())


if __name__=='__main__':
    RENDER=sys.argv.pop(1)
    unittest.main()
