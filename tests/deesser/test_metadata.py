#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""parameters.json must mirror the genuine DeEsser parameter list (names, ranges, defaults, choice order)
and the physical index order consumed by Processor::setParameter."""
import json, sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
meta = json.loads((root / "plugins/deesser/parameters.json").read_text())
p = meta["parameters"]
expect = [
    ("freq", "Freq", 2000, 16000, 5506, "Hz", "float", True),
    ("threshold", "Threshold", -80, 0, 0, "dB", "float", False),
    ("sidechain", "SideChain", 0, 1, 1, "", "choice", False),
    ("audio", "Audio", 0, 1, 0, "", "choice", False),
    ("monitor", "Monitor", 0, 1, 0, "", "choice", False),
    ("bypass", "Bypass", 0, 1, 0, "", "bool", False),
]
assert len(p) == len(expect), "six genuine automatable parameters"
for i, (pid, name, lo, hi, dflt, unit, typ, log) in enumerate(expect):
    q = p[i]
    got = (q["id"], q["name"], q["min"], q["max"], q["default"], q["unit"], q["type"], q["logarithmic"])
    assert q["index"] == i and got == (pid, name, lo, hi, dflt, unit, typ, log), f"index {i}: {got}"
assert p[2]["choices"] == ["BandPass", "HighPass"]
assert p[3]["choices"] == ["Split", "Wideband"]
assert p[4]["choices"] == ["Output", "SideChain"]
assert p[1]["step"] == 0.1
# Freq host taper observed on the genuine plugin: 1001 steps, 2000*8^(k/1000); default raw 0.487 -> 5506 Hz.
assert p[0]["steps"] == 1001 and round(2000 * 8 ** 0.487) == p[0]["default"]
print("PASS deesser parameters.json matches genuine parameter list")
