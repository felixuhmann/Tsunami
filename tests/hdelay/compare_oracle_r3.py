#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Round-2 task3 feedback-law check vs PRIVATE genuine renders (renders not committed).
Thresholds: docs/evidence/hdelay/thresholds.json -> round2_task3 (declared 16:18Z before rendering)."""
import json, os, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
TH = json.load(open(os.path.join(HERE, "../../docs/evidence/hdelay/thresholds.json")))["round2_task3"]["feedback_law"]
T0 = 288000; AMP = 0.125
CASES = [  # label, evidence, delay samples, feedback, role
 ("f50_250ms", "20261027T161809-a7178e7c3aa94b229b481559c84b26db", 12000, 50, "fit"),
 ("f150_250ms", "20261027T161833-16023dea7b7c4470aeb40b38e3cb2989", 12000, 150, "fit (repeat2/repeat1 only)"),
 ("f90_250ms", "20261027T161848-3fa0dd0d1c9140ab9a3f1ff8c01810a4", 12000, 90, "held-out (see note)"),
 ("r2_fb25_500ms", "20261027T161352-b7e28129e2594759ace2a14f85b92deb", 24000, 25, "held-out (see note)"),
]
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    return list(struct.unpack("<%df" % (n//4), b[i+8:i+8+n]))
def reps(y, D, n=3):
    out = []
    for k in range(1, n+1):
        c = T0 + k*D + (k-1); lo, hi = c-40, c+41
        if hi > len(y): break
        out.append({"peak": max(range(lo, hi), key=lambda j: abs(y[j])) - T0, "dc": sum(y[lo:hi])/AMP})
    return out
def main(render, root):
    rep = {"thresholds": "docs/evidence/hdelay/thresholds.json#round2_task3", "cases": []}; ok = True
    for label, ev, D, fb, role in CASES:
        d = os.path.join(root, ev); x = rd(os.path.join(d, "input.wav")); g = rd(os.path.join(d, "output.wav"))
        note = 10/19 if D == 12000 else 13/19
        with tempfile.TemporaryDirectory() as t:
            open(t+"/i", "wb").write(struct.pack("<%df" % len(x), *x))
            subprocess.check_call([render, t+"/i", t+"/o", "48000", "512", "1=%r" % note, "2=%d" % fb, "3=2", "4=100", "5=0"])
            y = list(struct.unpack("<%df" % len(x), open(t+"/o", "rb").read()))
        G, C = reps(g, D), reps(y, D)
        ratios = [{"genuine": G[i+1]["dc"]/G[i]["dc"], "core": C[i+1]["dc"]/C[i]["dc"]} for i in range(len(G)-1)]
        for r in ratios: r["rel_err"] = abs(r["core"]-r["genuine"])/abs(r["genuine"])
        case = {"label": label, "evidence": ev, "role": role, "genuine": G, "core": C, "ratios": ratios,
                "max_abs_err_norm_after_impulse": max(abs(y[k]-g[k]) for k in range(T0, len(x)))/AMP}
        th = next((v for k, v in TH["held_out"].items() if k.startswith(label.split("_")[0])), None)
        if role.startswith("held-out") and th:
            cok = ratios[0]["rel_err"] <= th["repeat2_over_repeat1_dc_rel_err"]
            if "repeat3_over_repeat2_dc_rel_err" in th and len(ratios) > 1: cok &= ratios[1]["rel_err"] <= th["repeat3_over_repeat2_dc_rel_err"]
            if "peak_offsets_exact" in th: cok &= [r["peak"] for r in G] == th["peak_offsets_exact"] == [r["peak"] for r in C]
            case["verdict"] = "PASS" if cok else "FAIL"; ok &= cok
        rep["cases"].append(case)
    rep["pass"] = bool(ok); print(json.dumps(rep, indent=1)); return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "/home/felix/work/tsunami-reference/runtime/evidence/hdelay"))
