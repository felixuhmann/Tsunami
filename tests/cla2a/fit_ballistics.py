# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the CHARACTERIZED opto time constants (Processor::TimeConstants) to
genuine 1 kHz step-burst renders ONLY (never the default evaluation renders
or held-out material). Input: -30/-6 dBFS bursts (1 s, 1.5 s, 3 s, 0.4 s,
3 s), mono int16 48 kHz; PR 46 and PR 70 Comp, analog Off.
Usage: python3 fit_ballistics.py DRIVER EVIDENCE_ROOT
Metric: 10 ms RMS gain envelope (dB) model vs genuine, first 50 ms excluded.
"""
import itertools, math, subprocess, sys, wave
import numpy as np
RENDERS = {"20261027T161805-d04b65dc970e459d8aebd37743f8fd67": 46,
           "20261027T161819-a72d3ca77ac14567b1169039e7d8c9d0": 70}
W = 480


def load(root, ev):
    y = np.load(f"{root}/{ev}/output.npy").reshape(-1).astype(float)
    w = wave.open(f"{root}/{ev}/input.wav")
    x = np.frombuffer(w.readframes(w.getnframes()), "<i2") / 32767.0
    return x, y


def env(s):
    return 10 * np.log10(np.add.reduceat(s ** 2, np.arange(0, len(s), W)) + 1e-30)


def model(driver, x, pr, t):
    st = np.repeat(x.astype(np.float32), 2)
    args = [driver, f"2={pr}", "5=2"] + ([("t=" + ",".join(f"{v:.6g}" for v in t))] if t is not None else [])
    r = subprocess.run(args, input=st.tobytes(), capture_output=True, check=True)
    return np.frombuffer(r.stdout, np.float32)[0::2].astype(float)


def errors(driver, data, t):
    out = {}
    for ev, (x, ge, pr) in data.items():
        me = env(model(driver, x, pr, t)) - env(x)
        out[ev] = float(np.sqrt(np.mean((me[5:] - ge[5:]) ** 2)))
    return out


def main():
    driver, root = sys.argv[1:3]
    data = {}
    for ev, pr in RENDERS.items():
        x, y = load(root, ev)
        data[ev] = (x, env(y) - env(x), pr)
    cost = lambda t: sum(v ** 2 for v in errors(driver, data, t).values())
    t = [1.0, 10.0, 10.0, 60.0, 0.06, 1.5]  # provisional start
    best = cost(t)
    print("start", errors(driver, data, t))
    step = 2.0
    while step > 1.09:
        improved = False
        for i, f in itertools.product(range(6), (step, 1 / step)):
            c = list(t); c[i] *= f
            v = cost(c)
            if v < best:
                print("improve", [round(q, 5) for q in c], round(v, 4), flush=True)
                best, t, improved = v, c, True
        if not improved:
            step = math.sqrt(step)
    print("fit", [round(v, 6) for v in t], errors(driver, data, t))


if __name__ == "__main__":
    main()
