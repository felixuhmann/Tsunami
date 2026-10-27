# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare candidate renderer with private oracle captures; no numpy required."""
import argparse, array, ast, hashlib, json, math, pathlib, struct, subprocess, wave
p=argparse.ArgumentParser();p.add_argument('renderer');p.add_argument('capture');p.add_argument('controls',nargs='*',type=float);a=p.parse_args()
root=pathlib.Path(a.capture); result=json.loads((root/'result.json').read_text())
assert result['status']=='ok' and result['host']['component']=='S1 Imager Stereo'
with wave.open(str(root/'input.wav'),'rb') as f:
 assert f.getsampwidth()==2 and f.getnchannels()==2
 raw=f.readframes(f.getnframes());samples=struct.unpack('<'+'h'*(len(raw)//2),raw)
# Pedalboard AudioFile PCM16 conversion is signed value / 32767 (not /32768).
source=struct.pack('<'+'f'*len(samples),*(x/32767. for x in samples))
r=subprocess.run([a.renderer,*map(str,a.controls)],input=source,capture_output=True,check=True)
candidate=struct.unpack('<'+'f'*(len(r.stdout)//4),r.stdout)
b=(root/'output.npy').read_bytes();assert b[:8]==b'\x93NUMPY\x01\x00'
hlen=struct.unpack('<H',b[8:10])[0];header=ast.literal_eval(b[10:10+hlen].decode());assert header['descr']=='<f4' and not header['fortran_order']
n=header['shape'][1];ref=struct.unpack('<'+'f'*(2*n),b[10+hlen:]);assert len(candidate)==2*n
errors=[candidate[2*i+c]-ref[c*n+i] for c in range(2) for i in range(n)]
report={'component':'S1 Imager Stereo','controls':a.controls,'frames':n,'max_abs':max(map(abs,errors)),'rms':math.sqrt(sum(e*e for e in errors)/len(errors)),'reference_samples_sha256':hashlib.sha256(b[10+hlen:]).hexdigest(),'capture':str(root),'readback':result['host']['controls']}
print(json.dumps(report,indent=2));assert report['max_abs']<2e-6
