# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply a checked unified diff to ONE pristine dependency file, writing a build-local copy.

Strict and deterministic: the input must hash to the expected value (LF-normalized, so a CRLF
checkout on Windows maps to the same pinned bytes), every hunk must match at its stated line with
no fuzz or offset, and the output must hash to the expected value. The dependency tree itself is
never written.
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

HUNK = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')


def lf_lines(path):
    text = path.read_bytes().replace(b'\r\n', b'\n').decode('utf-8')
    return text, [line + '\n' for line in text.split('\n')[:-1]] + ([text.rsplit('\n', 1)[-1]] if not text.endswith('\n') else [])


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def apply(source, patch):
    out, pos = [], 0
    lines = [line + '\n' for line in patch.split('\n')[:-1]]
    if not patch.endswith('\n'):
        raise ValueError('patch must end with a newline')
    i = 0
    while i < len(lines) and not lines[i].startswith('@@'):
        if lines[i].startswith(('--- ', '+++ ')) and not lines[i][4:].startswith(('a/src/wrapper/', 'b/src/wrapper/')):
            raise ValueError(f'unexpected file header {lines[i]!r}')
        i += 1
    if i == len(lines):
        raise ValueError('patch has no hunks')
    while i < len(lines):
        m = HUNK.match(lines[i])
        if not m:
            raise ValueError(f'unexpected patch line {i + 1}: {lines[i]!r}')
        start = int(m.group(1)) - 1 if m.group(2) != '0' else int(m.group(1))
        if start < pos:
            raise ValueError('overlapping or unordered hunks')
        out.extend(source[pos:start])
        pos = start
        old_count = int(m.group(2) or 1)
        new_count = int(m.group(4) or 1)
        seen_old = seen_new = 0
        i += 1
        while i < len(lines) and not lines[i].startswith('@@'):
            tag, body = lines[i][:1], lines[i][1:]
            seen_old += tag in (' ', '-')
            seen_new += tag in (' ', '+')
            if tag in (' ', '-'):
                if pos >= len(source) or source[pos] != body:
                    raise ValueError(f'hunk context mismatch at source line {pos + 1}')
                if tag == ' ':
                    out.append(body)
                pos += 1
            elif tag == '+':
                out.append(body)
            else:  # '\\ No newline at end of file' would change bytes: not supported
                raise ValueError(f'unexpected patch line {i + 1}: {lines[i]!r}')
            i += 1
        if (seen_old, seen_new) != (old_count, new_count):
            raise ValueError(f'hunk line counts {seen_old},{seen_new} != header {old_count},{new_count}')
    out.extend(source[pos:])
    return ''.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('patch', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-sha256', required=True)
    parser.add_argument('--patch-sha256', required=True)
    parser.add_argument('--output-sha256', required=True)
    args = parser.parse_args()
    text, source = lf_lines(args.source)
    patch_text, _ = lf_lines(args.patch)
    checks = (('pristine source', digest(text), args.source_sha256),
              ('patch', digest(patch_text), args.patch_sha256))
    for label, actual, expected in checks:
        if actual != expected:
            sys.exit(f'{label} {args.source if label != "patch" else args.patch}: sha256 {actual} != pinned {expected}')
    try:
        result = apply(source, patch_text)
    except ValueError as error:
        sys.exit(f'{args.patch}: {error}')
    if digest(result) != args.output_sha256:
        sys.exit(f'patched output sha256 {digest(result)} != pinned {args.output_sha256}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = result.encode('utf-8')
    if not args.output.is_file() or args.output.read_bytes() != data:  # avoid needless wrapper rebuilds
        args.output.write_bytes(data)
    print(f'patched {args.source.name}: source {args.source_sha256} + patch {args.patch_sha256} -> {args.output_sha256}')


if __name__ == '__main__':
    main()
