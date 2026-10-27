#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a small, framework-independent descriptor from physical metadata."""
import argparse
import json
from pathlib import Path
import math
import re


def taper_skew(p, lo, hi):
    """JUCE NormalisableRange skew: explicit taper.skew, or logarithmic (geometric midpoint at .5)."""
    taper = p.get('taper') or {}
    if 'skew' in taper:
        skew = float(taper['skew'])
    elif p.get('logarithmic', False) or taper.get('type') == 'logarithmic':
        if lo <= 0:
            raise ValueError('logarithmic taper needs a positive minimum')
        skew = math.log(0.5) / math.log((math.sqrt(lo * hi) - lo) / (hi - lo))
    else:
        skew = 1.0
    if not math.isfinite(skew) or skew <= 0:
        raise ValueError('taper skew must be finite and positive')
    return round(skew, 6)


def generate(data, plugin_id, product, core, editor=False):
    mono = data.get('supports_mono', False)
    if not isinstance(mono, bool):
        raise ValueError('supports_mono must be true or false')
    descriptors = []
    seen = set()
    for index, p in enumerate(data['parameters']):
        values = [float(p[k]) for k in ('min', 'max', 'default')]
        if (p['index'] != index or not re.fullmatch(r'[a-z][a-z0-9_]*', p['id'])
                or p['id'] in seen or not all(map(math.isfinite, values))
                or not values[0] <= values[2] <= values[1] or values[0] >= values[1]):
            raise ValueError('invalid parameter metadata')
        seen.add(p['id'])
        skew = taper_skew(p, values[0], values[1])
        choices = p.get('choices')
        kind = p.get('type')
        if kind == 'choice' or choices is not None:
            if (not isinstance(choices, list) or not choices or values[0] != 0
                    or values[1] != len(choices) - 1 or not all(isinstance(c, str) and c and '|' not in c for c in choices)):
                raise ValueError('choice parameters need labels for indices min=0..max=len(choices)-1')
        step = 1.0 if kind in ('bool', 'boolean', 'int', 'integer', 'enum', 'choice') or choices is not None else 0.0
        descriptors.append('    {' + ', '.join([
            json.dumps(p['id']), json.dumps(p['name']),
            *(f'{float(p[k])}f' for k in ('min', 'max', 'default')),
            json.dumps(p.get('unit', '')), f'{step}f',
            # Optional trailing fields stay omitted for linear, label-free controls (stable S1 output).
            *([f'{skew!r}f'] if skew != 1.0 or choices else []),
            *([json.dumps('|'.join(choices))] if choices else []),
        ]) + '}')
    return '''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Processor.hpp"
#include "Parameter.hpp"
%snamespace tsunami {
struct PluginSpec {
%s%s    using Core = %s;
    static constexpr const char* id = %s;
    static constexpr const char* name = %s;
    inline static constexpr Parameter parameters[] = {
%s
    };
};
}
''' % ('#include "Editor.hpp"\n' if editor else '',
       '''    static juce::AudioProcessorEditor* createEditor(juce::AudioProcessor& processor,
                                                     juce::AudioProcessorValueTreeState& state) {
        return tsunami::createPluginEditor(processor, state);
    }
''' if editor else '',
       # Opt-in 1-in/1-out layout (shared Adapter SupportsMono); omitted by default (stable S1 output).
       '    static constexpr bool supportsMono = true;\n' if mono else '', core, json.dumps(plugin_id), json.dumps(product), ',\n'.join(descriptors))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('metadata', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--id', required=True)
    parser.add_argument('--product', required=True)
    parser.add_argument('--core', required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate(json.loads(args.metadata.read_text()), args.id, args.product, args.core,
                                    editor=args.metadata.with_name('Editor.hpp').is_file()))
