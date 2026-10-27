#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Evaluate Processor against private original NS1 Mono held-out renders (oracle evidence dirs).
Evaluation only; thresholds are read from a predeclared JSON (default fidelity_thresholds.json['heldout_speech']).
PCM16 input decoding: the oracle host decodes 16-bit WAV as int/32767 (a scale scan on the stored
renders has a sharp optimum at 32767, not 32768), so the default --pcm16-scale is 32767."""
import json, os, subprocess, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
def read_wav(path, scale=32767.0):
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
    return (a.astype(np.float32) if tag == 3 else (a / scale).astype(np.float32)), sr
def main(render, report, evidence_dirs, scale=32767.0, th_file=None, th_key="heldout_speech"):
    th_file = th_file or os.path.join(HERE, "fidelity_thresholds.json")
    th = json.load(open(th_file))[th_key]["pass_if"]
    rows, ok = [], True
    for d in evidence_dirs:
        req = json.load(open(os.path.join(d, "request.json")))
        host = json.load(open(os.path.join(d, "host.json")))
        raw = float(host["controls"]["ns1"]["raw_readback"])  # "parameters" is the pre-set snapshot
        assert "bypass" not in host["controls"] and host["parameters"]["bypass"]["raw_value"] == 0.0
        ref = np.load(os.path.join(d, "output.npy")).astype(np.float32)
        ref = ref[0] if ref.ndim == 2 and ref.shape[0] < ref.shape[-1] else ref.reshape(-1)
        x, sr = read_wav(os.path.join(d, "input.wav"), scale)
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
    json.dump({"scope": "evaluation only (not used for tuning)", "thresholds": th, "thresholds_source": [th_file, th_key], "pcm16_scale": scale, "all_pass": bool(ok), "rows": rows}, open(report, "w"), indent=1)
    return 0 if ok else 1
if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("render"); ap.add_argument("report"); ap.add_argument("dirs", nargs="+")
    ap.add_argument("--pcm16-scale", type=float, default=32767.0)
    ap.add_argument("--thresholds"); ap.add_argument("--thresholds-key", default="heldout_speech")
    a = ap.parse_args()
    sys.exit(main(a.render, a.report, a.dirs, a.pcm16_scale, a.thresholds, a.thresholds_key))
