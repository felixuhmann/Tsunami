#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""HiPass/LoPass check vs PRIVATE genuine renders (not committed). Echo spectrum (8192 samples from
the 250 ms echo) relative to the unfiltered echo, genuine vs core. Thresholds: thresholds.json
round2_task3.filters (declared before rendering). Held-out: raw 0.7 (2.49 kHz) for both filters."""
import cmath, json, math, os, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
TH = json.load(open(os.path.join(HERE, "../../docs/evidence/hdelay/thresholds.json")))["round2_task3"]["filters"]["held_out"]
REF = "20261027T161055-650277653f4c467aa24abd0470c72940"
CASES = [("hp588", "20261027T161239-e40c66f68b6d4ea4ac501edba9e983da", 10, 0.49266503667481665, "fit"),
         ("hp154", "20261027T161917-f3d9fa9195d2448c829dd3f74e0645ba", 10, 0.3, "fit"),
         ("hp2490", "20261027T161959-6b5dede051b44022a1432d6812d19103", 10, 0.7, "hp_raw0.7"),
         ("lp588", "20261027T161305-66da9cf3e2244716b3fcc343d4c94b85", 11, 0.49266503667481665, "fit"),
         ("lp154", "20261027T161945-d951c5667d434249aa34c947cc8f0be1", 11, 0.3, "fit"),
         ("lp2490", "20261027T162013-5ece81aa08724556bc8343f4d00e0ad4", 11, 0.7, "lp_raw0.7")]
FR = [62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]; T = 288000 + 12000; N = 8192
def rd(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    return list(struct.unpack("<%df" % (n//4), b[i+8:i+8+n]))
def mag(s, f):
    w = -2j*math.pi*f/48000; return abs(sum(s[T-64+k]*cmath.exp(w*k) for k in range(N)))
def core(render, x, extra):
    with tempfile.TemporaryDirectory() as t:
        open(t+"/i", "wb").write(struct.pack("<%df" % len(x), *x))
        subprocess.check_call([render, t+"/i", t+"/o", "48000", "512", "1=%r" % (10/19), "2=0", "3=2", "4=100", "5=0", *extra])
        return list(struct.unpack("<%df" % len(x), open(t+"/o", "rb").read()))
def main(render, root):
    gref = rd(os.path.join(root, REF, "output.wav")); x = rd(os.path.join(root, REF, "input.wav")); cref = core(render, x, [])
    rep = {"cases": []}; ok = True
    for lab, ev, idx, raw, role in CASES:
        g = rd(os.path.join(root, ev, "output.wav")); y = core(render, rd(os.path.join(root, ev, "input.wav")), ["%d=%r" % (idx, raw)])
        rows = {}
        for f in FR:
            gd = 20*math.log10(mag(g, f)/mag(gref, f)); cd = 20*math.log10(mag(y, f)/mag(cref, f))
            rows[str(f)] = {"genuine_dB": round(gd, 3), "core_dB": round(cd, 3), "err_dB": round(cd-gd, 3)}
        e = max(abs(r["err_dB"]) for r in rows.values() if r["genuine_dB"] > -40)
        c = {"label": lab, "evidence": ev, "role": "held-out" if role in TH else "fit", "max_abs_err_dB(genuine>-40dB)": round(e, 3), "rows": rows}
        if role in TH: c["verdict"] = "PASS" if e <= TH[role]["max_abs_mag_err_dB"] else "FAIL"; ok &= c["verdict"] == "PASS"
        rep["cases"].append(c)
    rep["pass"] = bool(ok); print(json.dumps(rep, indent=1)); return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "/home/felix/work/tsunami-reference/runtime/evidence/hdelay"))
