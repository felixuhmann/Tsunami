#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Round-2 task4: settled read-phase law vs PRIVATE genuine renders (not committed).
Thresholds: docs/evidence/hdelay/thresholds.json#round2_task4 (declared before the renders)."""
import json, math, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
TH = json.load(open(os.path.join(HERE, "../../docs/evidence/hdelay/thresholds.json")))["round2_task4"]["cases"]
ROOT = "/home/felix/work/tsunami-reference/runtime/evidence/hdelay"
CASES = {"t4_down_bpm7of19": "20261027T162857-aac4bc080ba94dfa842d7f6b5032176c",
         "t4_up_bpm15of19": "20261027T162912-e6272e096d53474692daec5b441bceb4",
         # already-seen fit references (not hold-outs)
         "ref_250ms_down": "20261027T161055-650277653f4c467aa24abd0470c72940",
         "ref_500ms_up": "20261027T161352-b7e28129e2594759ace2a14f85b92deb"}
PRED = {"t4_down_bpm7of19": "sinc", "t4_up_bpm15of19": "delta", "ref_250ms_down": "sinc", "ref_500ms_up": "delta"}
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    return struct.unpack("<%df" % (n//4), b[i+8:i+8+n])
def sinc(t): return 1.0 if abs(t) < 1e-12 else math.sin(math.pi*t)/(math.pi*t)
def main():
    rep = {"thresholds_path": "docs/evidence/hdelay/thresholds.json#round2_task4", "cases": []}; ok = True
    for label, ev in CASES.items():
        x = rd(os.path.join(ROOT, ev, "input.wav")); g = rd(os.path.join(ROOT, ev, "output.wav"))
        T = max(range(len(x)), key=lambda k: abs(x[k])); a = x[T]
        P = max(range(T+100, min(len(g), T+150000)), key=lambda k: abs(g[k]))
        k = [g[P+n]/a for n in range(-20, 21)]; s = sum(k)
        best = min(((max(abs(k[j]/s - sinc(n - f/1000)) for j, n in enumerate(range(-20, 21))), f/1000)
                    for f in range(-500, 501)), key=lambda e: e[0])
        pred = [sinc(n - 1/32) if PRED[label] == "sinc" else (1.0 if n == 0 else 0.0) for n in range(-20, 21)]
        err = max(abs(k[j]/s - pred[j]) for j in range(41))
        c = {"label": label, "evidence": ev, "delay_samples": P - T, "dc": s, "prediction": PRED[label],
             "kernel_err": err, "best_free_sinc_frac": best[1], "best_free_sinc_err": best[0]}
        if label in TH:
            c["threshold"] = TH[label]["max_kernel_err"]; c["role"] = TH[label]["role"]
            c["verdict"] = "PASS" if err <= c["threshold"] else "FAIL"; ok &= err <= c["threshold"]
        rep["cases"].append(c)
    rep["pass"] = bool(ok); print(json.dumps(rep, indent=1)); return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main())
