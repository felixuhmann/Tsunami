#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare externally captured original .npy to interleaved candidate float32.

No vendor binary/audio is distributed. Controls and host provenance belong to the
caller. This numeric gate does not establish full-plugin or UI equivalence.
"""
import argparse
import ast
import json
import math
import pathlib
import struct


def compare(reference, candidate, channels):
    data = pathlib.Path(reference).read_bytes()
    if data[:8] != b'\x93NUMPY\x01\x00':
        raise ValueError('expected numpy v1 little-endian float32')
    length = struct.unpack_from('<H', data, 8)[0]
    meta = ast.literal_eval(data[10:10+length].decode())
    if meta['descr'] != '<f4' or meta['fortran_order']:
        raise ValueError('expected C-order little-endian float32')
    shape = meta['shape']
    if len(shape) != 2 or shape[0] != channels:
        raise ValueError('channel-major reference shape mismatch')
    raw = pathlib.Path(candidate).read_bytes()
    ref = struct.unpack('<%df' % (channels*shape[1]), data[10+length:])
    interleaved = struct.unpack('<%df' % (channels*shape[1]), raw)
    output = [interleaved[n*channels+c] for c in range(channels) for n in range(shape[1])]
    if not all(math.isfinite(v) for v in (*ref, *output)):
        raise ValueError('nonfinite audio')
    if not any(abs(v)>1e-8 for v in ref):
        raise ValueError('silent oracle is not a useful comparison')
    error = [a-b for a,b in zip(ref,output)]
    rms = math.sqrt(sum(v*v for v in error)/len(error))
    ref_rms = math.sqrt(sum(v*v for v in ref)/len(ref))
    return {'frames':shape[1], 'channels':channels, 'max_abs':max(map(abs,error)),
            'rms_error':rms, 'relative_rms':rms/ref_rms}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('candidate')
    parser.add_argument('--channels', type=int, required=True)
    parser.add_argument('--max-abs', type=float, required=True)
    parser.add_argument('--rms', type=float, required=True)
    args = parser.parse_args()
    result = compare(args.reference,args.candidate,args.channels)
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['max_abs']<=args.max_abs and result['rms_error']<=args.rms else 1)
