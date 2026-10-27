#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Evaluate Processor against private original NS1 Mono held-out renders (oracle evidence dirs).
Evaluation only; thresholds from fidelity_thresholds.json['heldout_speech'] declared before rendering."""
import json, os, subprocess, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
def read_wav(path):
    """Minimal RIFF reader: PCM16 or IEEE float32; returns channel 0 as float32."""
    import struct
    b = open(path, "rb").read(); i = 12; fmt = None; data = None
    while i + 8 <= len(b):
        cid, n = b[i:i + 4], struct.unpack("<I", b[i + 4:i + 8])[0]
        if cid == b"fmt ": fmt = struct.unpack("<HHIIHH", b[i + 8:i + 24])
        if cid == b"data": data = b[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    tag, ch, sr, _, _, bits = fmt
    if tag == 0xFFFE: tag = 3 if bits == 32 else 1
    a = np.frombuffer(data, dtype=np.float32 if tag == 3 else np.int16)
    a = a.reshape(-1, ch)[:, 0]
    return (a.astype(np.float32) if tag == 3 else (a / 32768.0).astype(np.float32)), sr
def main(render, report, evidence_dirs):
    th = json.load(open(os.path.join(HERE, "fidelity_thresholds.json")))["heldout_speech"]["pass_if"]
    rows, ok = [], True
    for d in evidence_dirs:
        req = json.load(open(os.path.join(d, "request.json")))
        host = json.load(open(os.path.join(d, "host.json")))
        raw = float(host["controls"]["ns1"]["raw_readback"])  # "parameters" is the pre-set snapshot
        assert "bypass" not in host["controls"] and host["parameters"]["bypass"]["raw_value"] == 0.0
        ref = np.load(os.path.join(d, "output.npy")).astype(np.float32)
        ref = ref[0] if ref.ndim == 2 and ref.shape[0] < ref.shape[-1] else ref.reshape(-1)
        x, sr = read_wav(os.path.join(d, "input.wav"))
        with tempfile.TemporaryDirectory() as td:
            fi, fo = os.path.join(td, "i"), os.path.join(td, "o"); x.tofile(fi)
            subprocess.run([render, str(sr), repr(raw * 100), str(req["block"]), fi, fo], check=True, capture_output=True)
            y = np.fromfile(fo, dtype=np.float32)
        e = y.astype(np.float64) - ref.astype(np.float64)
        cut = min(len(e), 19 * sr)
        rr = float(np.sqrt(np.mean(ref[:cut].astype(np.float64) ** 2)))
        nr = float(np.sqrt(np.mean(e[:cut] ** 2)) / rr); mx = float(np.max(np.abs(e[:cut])))
        p = nr <= th["nrmse_max"] and mx <= th["max_abs_error_max"]; ok &= p
        rows.append({"input": os.path.basename(req["input_original"]), "control": raw, "frames": len(ref), "seconds": len(ref) / sr,
                     "window_frames": int(cut), "nrmse": nr, "max_abs_error": mx, "pass": bool(p), "evidence": os.path.basename(d)})
        print(rows[-1])
    json.dump({"scope": "held-out speech evaluation (not used for tuning)", "thresholds": th, "all_pass": bool(ok), "rows": rows}, open(report, "w"), indent=1)
    return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3:]))
