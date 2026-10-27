# SPDX-License-Identifier: GPL-3.0-or-later
# Round2 task5: export 0x101100 read-pointer law (d += 1 - min(d/target, 2), same recursion as
# Processor.hpp) vs genuine echo positions; thresholds from thresholds.json#round2_task5.
import json, os, struct, glob, sys
HERE = os.path.dirname(os.path.abspath(__file__)); E = "/home/felix/work/tsunami-reference/runtime/evidence/hdelay/"
TH = json.load(open(os.path.join(HERE, "../../docs/evidence/hdelay/thresholds.json")))["round2_task5"]["cases"]
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    return struct.unpack("<%df" % (n//4), b[i+8:i+8+n])
def echo(d0, D, src, N=600000):
    d = d0
    for n in range(N):
        d += 1 - min(d/D, 2.0)
        if n - src >= d: return n
def gpeak(y, lo, hi): return max(range(lo, hi), key=lambda k: abs(y[k]))
rep = {"thresholds_path": "docs/evidence/hdelay/thresholds.json#round2_task5", "cases": {}}; ok = True
y1 = rd(glob.glob(E+"*154836-*/output.wav")[0]); y2 = rd(glob.glob(E+"*160946-*/output.wav")[0])
e1 = echo(18000, 24000, 0); e2 = echo(18000, 24000, gpeak(y1, 20000, 23000))
for k, y, pred, win in [("154836_first_echo", y1, e1, (20000, 23000)), ("154836_second_echo", y1, e2, (43000, 46000)),
                        ("160946_first_echo", y2, e1, (20000, 23000))]:
    g = gpeak(y, *win); err = pred - g; p = abs(err) <= TH[k]["max_abs_err_samples"]; ok &= p
    rep["cases"][k] = {"genuine": g, "law_prediction": pred, "err_samples": err, "pass": p}
# 666.7 ms held-out (162912): law prediction vs genuine (not tuned)
y3 = rd(glob.glob(E+"*162912-*/output.wav")[0]); g3 = gpeak(y3, 300000, 330000)
rep["info_162912_666ms"] = {"genuine_echo": g3, "law_prediction_glide_from_0": echo(18000, 32000, 288000)}
rep["pass"] = ok
json.dump(rep, open(os.path.join(HERE, "../../docs/evidence/hdelay/oracle-comparison-r5-glide.json"), "w"), indent=1)
print(json.dumps(rep, indent=1)); sys.exit(0 if ok else 1)
