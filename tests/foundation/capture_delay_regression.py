#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native capture regression: inject one slow X11 discovery, never fake a host/window.

Run under Xvfb with the same plugin/output/pluginval arguments as the capture gate.
"""
import importlib.util
from pathlib import Path
import time

spec = importlib.util.spec_from_file_location('capture', Path(__file__).with_name('linux_editor_capture.py'))
assert spec is not None and spec.loader is not None
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
original_query = gate.query
delayed = False


def slow_discovery(*args):
    global delayed
    if args == ('xwininfo', '-root', '-tree') and not delayed:
        # Strictness 5 only keeps Editor Automation open for ~1 second.
        # Reproduce scheduler/X11 latency crossing that lifecycle boundary.
        delayed = True
        time.sleep(1.25)
    return original_query(*args)


gate.query = slow_discovery
gate.main()
assert delayed, 'regression never exercised delayed discovery'
