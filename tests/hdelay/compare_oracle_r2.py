#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Round-2 held-out fidelity check vs PRIVATE genuine renders (renders not committed).
Thresholds are read from docs/evidence/hdelay/thresholds.json (predeclared before rendering)."""
import json, os, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
TH = json.load(open(os.path.join(HERE, "../../docs/evidence/hdelay/thresholds.json")))["cases"]
T0 = 288000  # impulse position in the synthesized input
# (label, evidence dir, core args in genuine parameter-index delivery order: DelayBpm, Feedback, Sync, Mix, Output)
CASES = [
 ("r1_ms_from_1_8_at120_fb0", "20261027T161055-650277653f4c467aa24abd0470c72940", ["1=%r" % (10/19), "2=0", "3=2", "4=100", "5=0"]),
 ("r2_ms_from_1_4_at120_fb25", "20261027T161352-b7e28129e2594759ace2a14f85b92deb", ["1=%r" % (13/19), "2=25", "3=2", "4=100", "5=0"]),
]
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    return list(struct.unpack("<%df" % (n//4), b[i+8:i+8+n]))
def main(render, root):
    rep = {"thresholds_path": "docs/evidence/hdelay/thresholds.json", "cases": []}; ok = True
    for label, ev, args in CASES:
        if not ev: rep["cases"].append({"label": label, "status": "no render"}); ok = False; continue
        th = TH[label]; d = os.path.join(root, ev); x = rd(os.path.join(d, "input.wav")); g = rd(os.path.join(d, "output.wav"))
        with tempfile.TemporaryDirectory() as t:
            open(t+"/i", "wb").write(struct.pack("<%df" % len(x), *x))
            subprocess.check_call([render, t+"/i", t+"/o", "48000", "512", *args])
            y = list(struct.unpack("<%df" % len(x), open(t+"/o", "rb").read()))
        amp = max(abs(v) for v in x); post = range(T0, len(x))
        err = max(abs(y[k]-g[k]) for k in post) / amp
        offs = th.get("predicted_peak_offsets", [th.get("predicted_peak_offset")])
        m = {"max_abs_err_norm_after_impulse": err, "repeats": []}; cok = err <= th["max_abs_err_norm"]
        for o in offs:
            lo, hi = T0+o-40, T0+o+41
            gp = max(range(lo, hi), key=lambda k: abs(g[k])) - T0; yp = max(range(lo, hi), key=lambda k: abs(y[k])) - T0
            gd, yd = sum(g[lo:hi])/amp, sum(y[lo:hi])/amp
            r = {"predicted": o, "genuine_peak": gp, "core_peak": yp, "genuine_dc": gd, "core_dc": yd, "dc_rel_err": abs(yd-gd)/max(abs(gd), 1e-12)}
            cok &= gp == o and yp == o
            if "repeat_dc_rel_err" in th: cok &= r["dc_rel_err"] <= th["repeat_dc_rel_err"]
            m["repeats"].append(r)
        rep["cases"].append({"label": label, "evidence": ev, "core_args": args, "metrics": m, "verdict": "PASS" if cok else "FAIL"}); ok &= cok
    rep["pass"] = bool(ok); print(json.dumps(rep, indent=1)); return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "/home/felix/work/tsunami-reference/runtime/evidence/hdelay"))
