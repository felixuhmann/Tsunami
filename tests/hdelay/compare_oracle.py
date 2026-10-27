#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Full-plugin fidelity check vs PRIVATE genuine renders (paths not committed data).
Thresholds are predeclared here before comparison."""
import json, struct, subprocess, sys, tempfile, os
THRESH = {"peak_index_exact": True, "max_abs_err_norm": 2e-3, "repeat_dc_rel_err": 5e-3}
CASES = [  # (label, genuine evidence dir, core params index=value)
 ("fb0_375ms", "20261027T100439-e9f996eeafa34fa1aa58d94662f6b2a7", ["0=375", "2=0", "4=100", "5=0"]),
 ("fb50_375ms", "20261027T153826-4ddf375da6324f8bafda1a72c0652ec1", ["0=375", "2=50", "4=100", "5=0"]),
]
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]; f = b.find(b"fmt "); fmt = 3 if struct.unpack("<H", b[f+22:f+24])[0] == 32 else 1
    return list(struct.unpack("<%df" % (n//4), b[i+8:i+8+n])) if fmt == 3 else [s/32768 for s in struct.unpack("<%dh" % (n//2), b[i+8:i+8+n])]
def main(render, root):
    rep = {"thresholds": THRESH, "cases": []}; ok = True
    for label, ev, params in CASES:
        d = os.path.join(root, ev); x = rd(os.path.join(d, "input.wav")); g = rd(os.path.join(d, "output.wav"))
        with tempfile.TemporaryDirectory() as t:
            open(t+"/i", "wb").write(struct.pack("<%df" % len(x), *x))
            subprocess.check_call([render, t+"/i", t+"/o", "48000", "512", *params])
            y = list(struct.unpack("<%df" % len(x), open(t+"/o", "rb").read()))
        amp = max(abs(v) for v in x); err = max(abs(a-b) for a, b in zip(y, g)) / amp
        pk = lambda s, lo, hi: max(range(lo, hi), key=lambda k: abs(s[k]))
        reps = [(17985, 18016), (35970, 36032)]
        m = {"max_abs_err_norm": err, "repeats": []}
        for lo, hi in reps:
            gd, yd = sum(g[lo:hi])/amp, sum(y[lo:hi])/amp
            if abs(gd) < 1e-6 and abs(yd) < 1e-6: continue
            r = {"window": [lo, hi], "genuine_peak": pk(g, lo, hi), "core_peak": pk(y, lo, hi), "genuine_dc": gd, "core_dc": yd,
                 "dc_rel_err": abs(yd-gd)/max(abs(gd), 1e-12)}
            m["repeats"].append(r); ok &= r["genuine_peak"] == r["core_peak"] and r["dc_rel_err"] <= THRESH["repeat_dc_rel_err"]
        ok &= err <= THRESH["max_abs_err_norm"]
        rep["cases"].append({"label": label, "evidence": ev, "params": params, "metrics": m})
    rep["pass"] = bool(ok); print(json.dumps(rep, indent=1)); return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "/home/felix/work/tsunami-reference/runtime/evidence/hdelay"))
