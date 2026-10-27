#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Binary stdin/stdout exchange of the deesser_render test renderer (used by mono_host_smoke.py).

The renderer exchanges planar float32 over pipes. A text-mode stream (e.g. the Windows CRT
default) would translate CR/LF or stop at 0x1a. Feeds finite float32 samples whose bytes contain
0x0d, 0x0a and 0x1a, with Bypass = 1 (the core copies input to output unchanged), and requires
exit 0 and byte-identical stdout, mono and stereo.

usage: test_renderer_streams.py path/to/deesser_render
"""
import math
import struct
import subprocess
import sys

# Little-endian float32 words built from the bytes a text-mode stream would alter.
WORDS = (b'\x0d\x0a\x1a\x3f', b'\x1a\x0d\x0a\x3e', b'\x0a\x1a\x0d\xbe', b'\x0d\x0d\x0a\x3d', b'\x1a\x1a\x1a\x3c', b'\x0a\x0a\x0d\xbf')


def payload(channels, frames):
    samples = [struct.unpack('<f', WORDS[i % len(WORDS)])[0] for i in range(channels * frames)]
    assert all(math.isfinite(v) and abs(v) < 1 for v in samples)
    data = struct.pack(f'<{len(samples)}f', *samples)
    assert all(b in data for b in (b'\r', b'\n', b'\x1a', b'\r\n'))
    return data


def main():
    renderer = sys.argv[1]
    for channels in (1, 2):
        frames = 96
        data = payload(channels, frames)
        # rate channels frames block, then freq threshold sidechain audio monitor bypass (physical; Bypass = 1).
        args = [renderer, '48000', str(channels), str(frames), '32', '5506', '-40', '1', '0', '0', '1']
        result = subprocess.run(args, input=data, capture_output=True)
        out = result.stdout
        first = next((i for i in range(min(len(out), len(data))) if out[i] != data[i]), None)
        print(f'channels={channels} exit={result.returncode} in={len(data)} out={len(out)} first_diff={first}'
              f' out_head={out[:12].hex()} in_head={data[:12].hex()}')
        assert result.returncode == 0, f'renderer exit {result.returncode} (3 = short stdin read, 4 = short stdout write)'
        assert out == data, f'stdout differs from stdin: {len(out)} vs {len(data)} bytes, first difference at {first}'
    print('PASS renderer binary stdin/stdout exchange (CR/LF/0x1a payload, bypass)')


if __name__ == '__main__':
    main()
