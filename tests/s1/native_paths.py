# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve the exact packaged Windows VST3 module before host loading/hashing."""
from pathlib import Path


def resolve_native_module(plugin: Path, system: str) -> Path:
    if system not in ('Windows', 'Linux'):
        raise ValueError(f'Unsupported native platform: {system}')
    plugin = plugin.resolve(strict=True)
    if plugin.is_dir():
        if system == 'Windows':
            return (plugin / 'Contents/x86_64-win' / plugin.name).resolve(strict=True)
        return (plugin / 'Contents/x86_64-linux' / (plugin.stem + '.so')).resolve(strict=True)
    return plugin
