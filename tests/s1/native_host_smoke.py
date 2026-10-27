#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual native S1 VST3 host checks; no original vendor code or custom host."""
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path
import numpy as np
import pedalboard
from pedalboard import load_plugin

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('plugin',type=Path)
p.add_argument('renderer',type=Path)
p.add_argument('receipt',type=Path)
a=p.parse_args()
plugin=a.plugin.resolve(strict=True)
host=load_plugin(str(plugin))
parameters=list(host.parameters.items())
assert len(parameters)==9, [k for k,v in parameters]
# The generated JUCE descriptor registers physical controls in Processor order.
controls=[-6.,2.,-22.5,-30.,0.,0.,0.,0.,0.]
minimum=[-24.,0.,-45.,-90.,0.,0.,0.,0.,0.]
maximum=[0.,3.,45.,90.,1.,1.,1.,1.,1.]
raw=[(v-lo)/(hi-lo) for v,lo,hi in zip(controls,minimum,maximum)]
for (_,parameter),value in zip(parameters,raw):parameter.raw_value=value
saved=host.raw_state
for _,parameter in parameters:parameter.raw_value=.9
host.raw_state=saved
for (_,parameter),value in zip(parameters,raw):assert abs(parameter.raw_value-value)<1e-6
fresh=load_plugin(str(plugin));fresh.raw_state=saved
for (_,parameter),value in zip(fresh.parameters.items(),raw):assert abs(parameter.raw_value-value)<1e-6
signal=np.random.default_rng(27).normal(0,.1,(2,4096)).astype(np.float32)
r=subprocess.run([str(a.renderer),*map(str,controls)],input=signal.T.copy().tobytes(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
portable=np.frombuffer(r.stdout,dtype='<f4').reshape(-1,2).T
assert portable.shape==signal.shape
cases=[]
for rate in (44100,48000,96000,192000):
 for block in (1,17,64,257,1024):
  y=host(signal,rate,buffer_size=block,reset=True)
  z=fresh(signal,rate,buffer_size=block,reset=True)
  assert y.shape==signal.shape and np.isfinite(y).all()
  error=float(np.max(np.abs(y-portable)));assert error<2e-6,(rate,block,error)
  assert np.array_equal(y,z),'fresh state differs'
  cases.append({'rate':rate,'block':block,'portable_max_abs':error,'output_sha256':hashlib.sha256(y.tobytes()).hexdigest()})
parameters[-1][1].raw_value=1
bypassed=host(signal,48000,buffer_size=64,reset=True)
assert np.array_equal(bypassed,signal),'explicit replacement bypass is not exact'
parameters[-1][1].raw_value=0
silent=host(np.zeros_like(signal),48000,buffer_size=64,reset=True)
assert not np.count_nonzero(silent),'matrix generated audio from silence'
assert host.reported_latency_samples==0
module=plugin/'Contents/x86_64-linux'/f'{plugin.stem}.so' if plugin.is_dir() else plugin
receipt={'scope':'native Linux S1 VST3 audio/state/control smoke; not native Windows, original automation, original-layout UI or full equivalence','platform':platform.platform(),'pedalboard':pedalboard.__version__,'plugin':str(plugin),'module_sha256':hashlib.sha256(module.read_bytes()).hexdigest(),'renderer_sha256':hashlib.sha256(a.renderer.read_bytes()).hexdigest(),'parameters':[k for k,v in parameters],'cases':cases,'state_restore_all_parameters':True,'fresh_state_exact':True,'bypass_exact':True,'silence_exact':True,'latency_samples':host.reported_latency_samples}
a.receipt.parent.mkdir(parents=True,exist_ok=True);a.receipt.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'native_cases':len(cases),'max_error':max(c['portable_max_abs'] for c in cases),'state':'all nine restored; fresh output exact','bypass':'exact','silence':'exact','latency':host.reported_latency_samples,'receipt':str(a.receipt)},indent=2))
