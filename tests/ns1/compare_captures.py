#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the Tsunami NS1 Processor against private original NS1 Mono captures.

Captures are NOT part of this repository. Pass --capture DIR (channel-major
float32 .npy pairs + index.json). Thresholds come from fidelity_thresholds.json,
declared before comparison. Exit 0 only if every established-control row passes.
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", required=True)
    ap.add_argument("--render", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--require-offgrid", action="store_true",
                    help="also gate the predeclared comparison controls (docs/evidence/ns1/thresholds.json)")
    a = ap.parse_args()
    th = json.load(open(os.path.join(HERE, "fidelity_thresholds.json")))
    est = set(th["established_controls"])
    if a.require_offgrid:
        est |= set(json.load(open(os.path.join(HERE, "..", "..", "docs", "evidence", "ns1", "thresholds.json")))["comparison_controls"])
    idx = json.load(open(os.path.join(a.capture, "index.json")))
    rows, ok, not_compared = [], True, []
    for r in idx["runs"]:
        rid = r.get("id")
        if r.get("plugin") != "NS1 Mono" or r.get("bypass_readback", 0) != 0:
            not_compared.append({"id": rid, "reason": "not NS1 Mono or bypass engaged"})
            continue
        if not r.get("fresh_instance") or r.get("warmup_frames", 0) != 0 or r["input_shape"][0] != 1:
            not_compared.append({"id": rid, "reason": "not a fresh-instance, zero-warmup mono capture (e.g. reset sequence/state carry); this harness cannot reproduce it"})
            continue
        inp = os.path.join(a.capture, rid + "-input.npy")
        if not os.path.exists(inp):
            not_compared.append({"id": rid, "reason": "input array missing"})
            continue
        x = np.load(inp)[0].astype(np.float32)
        ref = np.load(os.path.join(a.capture, r["output_file"]))[0].astype(np.float32)
        c = float(r["control_readback"])
        with tempfile.TemporaryDirectory() as td:
            fi, fo = os.path.join(td, "i.f32"), os.path.join(td, "o.f32")
            x.tofile(fi)
            subprocess.run([a.render, str(r["sample_rate"]), repr(c * 100.0), str(r["block_size"]), fi, fo], check=True, capture_output=True)
            y = np.fromfile(fo, dtype=np.float32)
        e = (y.astype(np.float64) - ref.astype(np.float64))
        rr = float(np.sqrt(np.mean(ref.astype(np.float64) ** 2)))
        nr = float(np.sqrt(np.mean(e ** 2)) / rr) if rr > 0 else None
        mx = float(np.max(np.abs(e)))
        established = c in est
        raw_pass = mx <= th["pass_if"]["max_abs_error_max"] and (nr is None or nr <= th["pass_if"]["nrmse_max"])
        cut = int(th["demo_policy"]["pre_demo_window_seconds"] * r["sample_rate"])
        pre = None
        if len(ref) > cut:
            ew, rw = e[:cut], ref[:cut].astype(np.float64)
            prr = float(np.sqrt(np.mean(rw ** 2)))
            pre = {"frames": cut, "nrmse": float(np.sqrt(np.mean(ew ** 2)) / prr) if prr > 0 else None,
                   "max_abs_error": float(np.max(np.abs(ew)))}
            passed = pre["max_abs_error"] <= th["pass_if"]["max_abs_error_max"] and (pre["nrmse"] is None or pre["nrmse"] <= th["pass_if"]["nrmse_max"])
        else:
            passed = raw_pass
        nz = np.nonzero(np.abs(e) > th["pass_if"]["max_abs_error_max"])[0]
        rows.append({"id": rid, "control": c, "sample_rate": r["sample_rate"], "block": r["block_size"],
                     "established_control": established, "nrmse": nr, "max_abs_error": mx,
                     "reference_rms": rr, "first_exceed_index": int(nz[0]) if len(nz) else None,
                     "whole_buffer_pass": bool(raw_pass), "pre_demo_window": pre,
                     "pass": bool(passed)})
        if established and not passed:
            ok = False
    rep = {"scope": th["scope"], "thresholds": th["pass_if"], "equivalence_claim": False,
           "established_rows": sum(r["established_control"] for r in rows),
           "established_pass": sum(r["established_control"] and r["pass"] for r in rows),
           "all_established_pass": ok, "rows": rows, "not_compared": not_compared}
    json.dump(rep, open(a.report, "w"), indent=1)
    for r in rows:
        print(f'{r["id"]:32s} c={r["control"]:<5} est={int(r["established_control"])} nrmse={r["nrmse"]} max={r["max_abs_error"]:.3g} {"PASS" if r["pass"] else "FAIL"} raw={"PASS" if r["whole_buffer_pass"] else "FAIL"}')
    for n in not_compared:
        print(f'{str(n["id"]):32s} NOT COMPARED: {n["reason"]}')
    print("ALL ESTABLISHED PASS" if ok else "FIDELITY GATE FAILS")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
