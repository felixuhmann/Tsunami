#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the portable DeEsser renderer against stored genuine oracle renders.

Usage: compare_oracle.py RENDER_BINARY EVIDENCE_ROOT THRESHOLDS_JSON [REPORT_JSON]
Reads only stored oracle evidence (request.json, input.wav, output.wav); never invokes the oracle.
"""
import array, json, math, struct, subprocess, sys
from pathlib import Path

# Genuine oracle parameter keys -> portable processor index and raw->physical mapping.
def physical(params):
    p = {"freq": 5506.0, "threshold": 0.0, "sidechain": 1.0, "audio": 0.0, "monitor": 0.0, "bypass": 0.0}
    for key, spec in params.items():
        if "value" in spec:
            p[key] = float(spec["value"])
        elif "raw" in spec:
            raw = float(spec["raw"])
            if key == "freq":
                p[key] = 2000.0 * 8.0 ** raw
            elif key == "threshold":
                p[key] = -80.0 + 80.0 * raw
            else:
                p[key] = raw
    return [p["freq"], p["threshold"], p["sidechain"], p["audio"], p["monitor"], p["bypass"]]

# Integer PCM is decoded with 1/(2^(bits-1)-1), matching the oracle host's decoder
# (measured: genuine/candidate gain ratio was exactly 32768/32767 with 1/32768 decoding).
def readwav(path):
    b = Path(path).read_bytes(); pos = 12; fmt = raw = None
    while pos + 8 <= len(b):
        tag = b[pos:pos+4]; n = struct.unpack_from('<I', b, pos+4)[0]; v = b[pos+8:pos+8+n]; pos += 8 + n + (n % 2)
        if tag == b'fmt ': fmt = struct.unpack_from('<HHIIHH', v)
        if tag == b'data': raw = v
    code, ch, rate, _, _, bits = fmt
    if code == 0xFFFE: code = 3 if bits == 32 else 1
    if code == 3 and bits == 32: a = array.array('f'); a.frombytes(raw); s = list(a)
    elif code == 1 and bits == 16: a = array.array('h'); a.frombytes(raw); s = [x / 32767 for x in a]
    elif code == 1 and bits == 24:
        s = [int.from_bytes(raw[i:i+3], 'little', signed=True) / 8388607 for i in range(0, len(raw) - 2, 3)]
    else: raise SystemExit(f"unsupported wav {fmt} {path}")
    frames = len(s) // ch
    return rate, [[s[i*ch + c] for i in range(frames)] for c in range(ch)]

def main():
    binary, root, thr = sys.argv[1], Path(sys.argv[2]), json.loads(Path(sys.argv[3]).read_text())
    crit = thr["pass_criteria"]; cases = []
    for d in sorted(root.iterdir()):
        req = d / "request.json"
        if not req.exists() or not (d / "output.wav").exists(): continue
        q = json.loads(req.read_text())
        if q.get("action") != "render": continue
        rate, x = readwav(d / "input.wav"); _, ref = readwav(d / "output.wav")
        ch = len(x); frames = len(x[0]); phys = physical(q.get("params", {}))
        planar = array.array('f', [v for c in x for v in c])
        r = subprocess.run([binary, str(rate), str(ch), str(frames), str(q.get("block", 512))] + [repr(v) for v in phys],
                           input=planar.tobytes(), capture_output=True, check=True)
        y = array.array('f'); y.frombytes(r.stdout)
        err2 = ref2 = 0.0; mx = 0.0
        for c in range(ch):
            for i in range(min(frames, len(ref[c]))):
                e = y[c*frames + i] - ref[c][i]; err2 += e*e; ref2 += ref[c][i]**2; mx = max(mx, abs(e))
        res_db = 10*math.log10(err2/ref2) if err2 > 0 and ref2 > 0 else -math.inf
        ok = mx <= crit["max_abs_error"] and res_db <= crit["residual_db"]
        src = str(q.get("input_original", q.get("input")))
        cases.append({"evidence": d.name, "set": "heldout" if "heldout" in src else "calibrated", "component": q.get("component"), "input": Path(src).name, "input_sha256": q.get("input_sha256"),  # capture-ID + file name only; no private absolute paths in public evidence
                      "oracle_params": q.get("params", {}), "physical": phys, "max_abs_error": mx,
                      "residual_db": res_db if math.isfinite(res_db) else -999.0, "pass": ok})
    report = {"thresholds": thr, "cases": cases, "all_pass": all(c["pass"] for c in cases) and bool(cases)}
    text = json.dumps(report, indent=2)
    if len(sys.argv) > 4: Path(sys.argv[4]).write_text(text + "\n")
    for c in cases: print(f'{c["evidence"][:15]} {c["set"]:10s} {c["component"]:15s} {json.dumps(c["oracle_params"]):60s} max={c["max_abs_error"]:.3e} res={c["residual_db"]:.1f}dB {"PASS" if c["pass"] else "FAIL"}')
    return 0 if report["all_pass"] else 1

if __name__ == "__main__":
    sys.exit(main())
