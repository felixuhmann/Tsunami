# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a real oracle capture to the partial Processor (stdlib only).
Usage: python3 compare_oracle.py DRIVER EVIDENCE_DIR
Reads the exact capture input WAV; never synthesizes vendor output.
"""
import array
import ast
import json
import math
import pathlib
import struct
import subprocess
import sys
import wave


def load_npy(path):
    with open(path, "rb") as f:
        if f.read(6) != b"\x93NUMPY":
            raise ValueError("not NPY")
        major, _ = f.read(2)
        size = struct.unpack("<H" if major == 1 else "<I", f.read(2 if major == 1 else 4))[0]
        header = ast.literal_eval(f.read(size).decode("latin1"))
        if header["descr"] != "<f4" or header["fortran_order"]:
            raise ValueError("need little-endian C-order float32")
        values = array.array("f")
        values.frombytes(f.read())
    return header["shape"], values


INDEX = {"bypass": 0, "gain": 1, "peak_reduction": 2, "comp_limiter": 3, "hi_freq": 4,
         "analog": 5, "vu_display": 6, "mix": 7, "trim": 8, "auto_makeup": 9}
ENUMS = {"comp_limiter": ["Comp", "Limit"], "analog": ["50Hz", "60Hz", "Off"],
         "vu_display": ["In", "GR", "Out"], "auto_makeup": ["Off", "On"],
         "bypass": ["False", "True"]}


def physical(key, host_value):
    text = str(host_value).strip()
    if key in ENUMS:
        return float(ENUMS[key].index(text))
    return float(text)


def rms(values):
    return math.sqrt(sum(v * v for v in values) / max(1, len(values)))


def main():
    driver, evidence = sys.argv[1:3]
    evidence = pathlib.Path(evidence)
    result = json.loads((evidence / "result.json").read_text())
    assert result["status"] == "ok" and result["host"]["status"] == "ok"
    # Genuine effective control values reported by the host for this render.
    # "parameters" is the enumerated default state; "controls" holds the
    # values the host actually applied for this render and take precedence.
    params = dict(result["host"].get("parameters", {}))
    params.update(result["host"].get("controls", {}))
    args, settings = [], {}
    for key, index in INDEX.items():
        if key in params:
            settings[key] = physical(key, params[key]["host_value"])
            args.append(f"{index}={settings[key]}")
    with wave.open(str(evidence / "input.wav"), "rb") as wav:
        assert wav.getsampwidth() == 2 and wav.getnchannels() in (1, 2)
        assert wav.getframerate() == 48000
        channels = wav.getnchannels()
        pcm = array.array("h", wav.readframes(wav.getnframes()))
    if channels == 1:  # driver transport is stereo; duplicate, keep channel 0
        pcm = array.array("h", (v for v in pcm for _ in range(2)))
    # The genuine host decodes int16 WAV as v / 32767 (measured: dry-path ratio
    # is exactly 32768/32767 above the trim gain), so feed the same samples.
    samples = array.array("f", (v / 32767.0 for v in pcm))
    rendered = subprocess.run([driver, *args], input=samples.tobytes(), check=True, capture_output=True)
    candidate = array.array("f", rendered.stdout)
    shape, reference = load_npy(evidence / "output.npy")
    frames = len(samples) // 2
    assert shape in ((2, frames), (1, frames)), shape
    if shape[0] == 1:
        candidate = array.array("f", candidate[0::2])
        reference = list(reference)
    else:
        reference = [reference[c * frames + n] for n in range(frames) for c in range(2)]
    assert len(candidate) == len(reference)
    errors = [float(a) - float(b) for a, b in zip(candidate, reference)]
    tail = slice(len(reference) // 2, None)
    ref_rms, cand_rms = rms(reference[tail]), rms(candidate[tail])
    report = {"evidence": str(evidence), "settings": settings,
              "dry_subset": settings.get("bypass", 0) == 1 or settings.get("mix", 100) == 0,
              "samples": len(errors), "rms_error": rms(errors),
              "max_abs_error": max(map(abs, errors)),
              "rel_rms_error_db": 20 * math.log10(max(1e-30, rms(errors)) / max(1e-30, rms(reference))),
              "steady_level_diff_db": 20 * math.log10(max(1e-30, cand_rms) / max(1e-30, ref_rms)),
              "candidate_all_finite": all(map(math.isfinite, candidate))}
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
