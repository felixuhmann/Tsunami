# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive a build-local copy of a pinned upstream source file.

The input must match an exact SHA-256, every edit anchor must match exactly once, and the result
must match an exact output SHA-256 before anything is written. The input file is never modified;
an identical existing output is left untouched so incremental builds are not invalidated.
"""
import argparse, hashlib, json, os, sys


def fail(msg):
    sys.exit("derive_source: " + msg)


def main():
    ap = argparse.ArgumentParser()
    for opt in ("--input", "--input-sha256", "--edits", "--output", "--output-sha256"):
        ap.add_argument(opt, required=True)
    a = ap.parse_args()
    with open(a.input, "rb") as f:
        src = f.read()
    crlf = b"\r\n" in src
    if crlf:  # Windows git checkouts may convert line endings; hashes bind the LF-normalized text
        src = src.replace(b"\r\n", b"\n")
    if hashlib.sha256(src).hexdigest() != a.input_sha256:
        fail(f"input sha256 mismatch for {a.input}" + (" (after CRLF normalization)" if crlf else ""))
    if crlf:
        print(f"derive_source: CRLF input normalized to LF: {a.input}")
    with open(a.edits, "rb") as f:
        edits = json.load(f)
    if not isinstance(edits, list) or not all(
            isinstance(e, dict) and set(e) == {"find", "replace"}
            and all(isinstance(e[k], list) and all(isinstance(s, str) for s in e[k]) for k in e)
            for e in edits):
        fail("malformed edits: expected [{\"find\": [lines], \"replace\": [lines]}]")
    text = src.decode("utf-8")
    for i, e in enumerate(edits):
        find, replace = "\n".join(e["find"]), "\n".join(e["replace"])
        if not find or text.count(find) != 1:
            fail(f"edit {i} anchor must match exactly once (found {text.count(find) if find else 0})")
        text = text.replace(find, replace)
    out = text.encode("utf-8")
    if hashlib.sha256(out).hexdigest() != a.output_sha256:
        fail(f"output sha256 mismatch: got {hashlib.sha256(out).hexdigest()}")
    if os.path.exists(a.output):
        with open(a.output, "rb") as f:
            if f.read() == out:
                return
    os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
    tmp = a.output + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, a.output)


if __name__ == "__main__":
    main()
