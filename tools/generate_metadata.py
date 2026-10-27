#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a small, framework-independent descriptor from physical metadata."""
import argparse
import json
from pathlib import Path
import math
import re


def generate(data, plugin_id, product, core, editor=False):
    descriptors = []
    seen = set()
    for index, p in enumerate(data['parameters']):
        if p.get('logarithmic', False):
            raise ValueError('logarithmic mapping is not implemented')
        values = [float(p[k]) for k in ('min', 'max', 'default')]
        if (p['index'] != index or not re.fullmatch(r'[a-z][a-z0-9_]*', p['id'])
                or p['id'] in seen or not all(map(math.isfinite, values))
                or not values[0] <= values[2] <= values[1] or values[0] >= values[1]):
            raise ValueError('invalid parameter metadata')
        seen.add(p['id'])
        descriptors.append('    {' + ', '.join([
            json.dumps(p['id']), json.dumps(p['name']),
            *(f'{float(p[k])}f' for k in ('min', 'max', 'default')),
            json.dumps(p.get('unit', '')), '1.0f' if p.get('type') in ('bool', 'boolean', 'int', 'integer', 'enum') else '0.0f',
        ]) + '}')
    return '''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Processor.hpp"
#include "Parameter.hpp"
%snamespace tsunami {
struct PluginSpec {
%s    using Core = %s;
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
''' if editor else '', core, json.dumps(plugin_id), json.dumps(product), ',\n'.join(descriptors))


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
