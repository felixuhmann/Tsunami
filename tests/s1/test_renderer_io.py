# SPDX-License-Identifier: GPL-3.0-or-later
"""Real portable-renderer binary stdin/stdout regression (run on each native OS)."""
import subprocess
import sys
from pathlib import Path

if __name__ == '__main__':
    renderer = Path(sys.argv[1]).resolve(strict=True)
    # Finite float32 payload includes CR, LF and Windows text-mode EOF bytes.
    payload = bytes.fromhex('0d0a1a3e 1a0a0dbe 1a0d0a3f 0a1a0d3e')
    result = subprocess.run([str(renderer), '0', '1', '0', '0', '0', '0', '0', '0', '1'],
                            input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    assert result.stdout == payload, f'binary bypass changed bytes: expected {payload.hex()}, got {result.stdout.hex()}'
    print('renderer binary stdin/stdout bypass exact including CR/LF/EOF bytes')
