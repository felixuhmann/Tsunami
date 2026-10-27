# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the CHARACTERIZED static curve (plugins/cla2a/CharacterizedCurve.h)
from genuine stepped-tone renders only (never the default evaluation renders).
Usage: python3 fit_static_curve.py BUILD_DIR EVIDENCE_ROOT
Stepped input: 1 kHz sine, peak -42..0 dBFS in 3 dB steps, 1.2 s each; the
steady gain is the RMS ratio over the last 0.4 s of each step.
"""
import array, math, pathlib, subprocess, sys, wave
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from compare_oracle import load_npy

HERE = pathlib.Path(__file__).resolve().parents[2]
HEADER = HERE / "plugins/cla2a/CharacterizedCurve.h"
SR, SEG = 48000, int(1.2 * 48000)
LEVELS = [-42 + 3 * i for i in range(15)]
D0, STEP, N = -16.0, 2.0, 26
PR_DRIVE = lambda pr: [-50, -33, 15.6, 31, 39][int(pr // 25)] + ((pr / 25) % 1) * ([-50, -33, 15.6, 31, 39, 39][int(pr // 25) + 1] - [-50, -33, 15.6, 31, 39][int(pr // 25)])
RENDERS = {  # evidence id -> (peak reduction, mode)
    "20261027T161007-313648bd4b8c49918a8190d416a7e7f5": (46, 0),
    "20261027T161031-c7231b197fdd4ef293009013796d70e9": (70, 0),
    "20261027T161215-25f76d944f6a45b7b15eae2ab7914407": (46, 1),
}
BASE_EVIDENCE = "20261027T161139-5bf980b98c6b44c4823fd815ffda0aaa"  # PR 25: no reduction


def steady(out, inp):
    res = []
    for i in range(len(LEVELS)):
        a, b = i * SEG + int(0.8 * SR), (i + 1) * SEG
        ry = math.sqrt(sum(float(v) ** 2 for v in out[a:b]) / (b - a))
        rx = math.sqrt(sum(float(v) ** 2 for v in inp[a:b]) / (b - a))
        res.append(20 * math.log10(ry / rx))
    return res


def interp(xs, ys, x):
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1])
    return ys[-1] + (ys[-1] - ys[-2]) / (xs[-1] - xs[-2]) * (x - xs[-1])


def write_header(tables, level_db):
    fmt = lambda t: ", ".join(f"{v:.3f}" for v in t)
    HEADER.write_text(f"""// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CHARACTERIZED, NOT RECOVERED. Static gain-reduction curve (dB) versus
// detector peak level (dB, input peak + recovered Peak Reduction drive),
// fitted by tests/cla2a/fit_static_curve.py to genuine stepped 1 kHz tone
// renders (runtime/evidence/cla2a {', '.join(k[:15] for k in RENDERS)};
// baseline {BASE_EVIDENCE[:15]}). The default evaluation renders were not
// used. Comp is constrained by data over detector about -34..+28 dB, Limit over
// -34..+8 dB; points outside are linear extrapolation. The 26-point count
// matches the reference kernel; the reference breakpoints are unknown.
// ProgrammeLevelDb: 1 kHz level offset between recovered makeup and the
// measured uncompressed gain (unrecovered programme-path filtering).
#include <array>
namespace tsunami::cla2a::Characterized {{
constexpr double DetectorDb0 = {D0}, DetectorStepDb = {STEP};
constexpr double ProgrammeLevelDb = {level_db:.3f};
constexpr std::array<double, {N}> CompGrDb{{{fmt(tables[0])}}};
constexpr std::array<double, {N}> LimitGrDb{{{fmt(tables[1])}}};
}}
""")


def main():
    build, root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    def load(ev):
        _, y = load_npy(root / ev / "output.npy")
        with wave.open(str(root / ev / "input.wav")) as w:
            x = [v / 32767.0 for v in array.array("h", w.readframes(w.getnframes()))]
        return x, y
    xin, ybase = load(BASE_EVIDENCE)
    base = steady(ybase, xin)[0]
    level_db = base - 8.8  # recovered makeup at Gain 40
    genuine = {ev: [g - base for g in steady(load(ev)[1], xin)] for ev in RENDERS}
    grid = [D0 + STEP * i for i in range(N)]
    tables = []
    for mode in (0, 1):
        pts = sorted((L + PR_DRIVE(pr), g) for ev, (pr, m) in RENDERS.items() if m == mode
                     for L, g in zip(LEVELS, genuine[ev]))
        tables.append([min(0.0, interp([p[0] for p in pts], [p[1] for p in pts], d)) for d in grid])
    stereo = array.array("f", (v for v in xin for _ in range(2))).tobytes()
    for it in range(6):
        write_header(tables, level_db)
        subprocess.run(["cmake", "--build", str(build), "--target", "cla2a_reference_driver"], check=True, capture_output=True)
        worst = 0.0
        corrections = [[[] for _ in grid] for _ in (0, 1)]
        for ev, (pr, mode) in RENDERS.items():
            out = subprocess.run([str(build / "cla2a_reference_driver"), "5=2", f"2={pr}", f"3={mode}"],
                                 input=stereo, check=True, capture_output=True).stdout
            sim = [g - base for g in steady(array.array("f", out)[0::2], xin)]
            for L, gs, gg in zip(LEVELS, sim, genuine[ev]):
                worst = max(worst, abs(gs - gg))
                d = L + PR_DRIVE(pr)
                k = min(N - 1, max(0, round((d - D0) / STEP)))
                corrections[mode][k].append(gg - gs)
        print(f"iteration {it}: worst steady error {worst:.3f} dB")
        for mode in (0, 1):
            for k in range(N):
                if corrections[mode][k]:
                    tables[mode][k] = min(0.0, tables[mode][k] + sum(corrections[mode][k]) / len(corrections[mode][k]))
    write_header(tables, level_db)


if __name__ == "__main__":
    main()
