#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture pluginval's real embedded editor; this script is not a plugin host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

TITLE = 'Tsunami Infrastructure Fixture'


def verify_validation(log):
    for name in ('Editor', 'Open editor whilst processing', 'Editor Automation'):
        assert f'Completed tests in pluginval / {name}\n' in log, f'{name} did not complete'
    assert f'Plugin name: {TITLE}\n' in log, 'wrong plugin identity'
    assert re.search(r'^SUCCESS\s*$', log, re.M), 'pluginval did not succeed'


def verify_identity(properties, launched_pid, module, maps):
    match = re.search(r'_NET_WM_PID\(CARDINAL\) = (\d+)', properties)
    assert match and int(match[1]) == launched_pid, 'window is not the launched in-process host'
    assert '"pluginval", "pluginval"' in properties, 'wrong window class'
    assert any(line.endswith(' ' + str(module)) for line in maps.splitlines()), 'exact native module is not loaded by window owner'


def query(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, timeout=5)


def main():
    from PIL import ImageGrab
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('host', type=Path)
    args = parser.parse_args()
    plugin = args.plugin.resolve(strict=True)
    module = (plugin / 'Contents/x86_64-linux' / (plugin.stem + '.so')).resolve(strict=True)
    host = args.host.resolve(strict=True)
    version = query(str(host), '--version').strip()
    assert version == 'pluginval - 1.0.4', version
    args.output.mkdir(parents=True, exist_ok=True)
    log_path = args.output / 'pluginval-editor.log'
    command = [str(host), '--validate-in-process', '--strictness-level', '5', '--random-seed', '17', '--validate', str(plugin)]
    capture = None
    with log_path.open('w') as log_file:
        process = subprocess.Popen(command, stdout=log_file, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while process.poll() is None and time.monotonic() < deadline:
                log = log_path.read_text()
                # This test keeps a real attached editor open while automating it.
                if capture is None and 'Starting tests in: pluginval / Editor Automation...' in log:
                    tree = query('xwininfo', '-root', '-tree')
                    for match in re.finditer(r'(0x[0-9a-f]+).*\("pluginval" "pluginval"\)\s+(\d+)x(\d+)', tree):
                        window, width, height = match.groups()
                        if int(width) < 400 or int(height) < 100:
                            continue
                        try:
                            properties = query('xprop', '-id', window, '_NET_WM_PID', 'WM_CLASS')
                            pid = process.pid
                            maps = Path(f'/proc/{pid}/maps').read_text()
                            verify_identity(properties, pid, module, maps)
                            info = query('xwininfo', '-id', window)
                            assert 'Map State: IsViewable' in info
                            x = int(re.search(r'Absolute upper-left X:\s+(-?\d+)', info)[1])
                            y = int(re.search(r'Absolute upper-left Y:\s+(-?\d+)', info)[1])
                            w = int(re.search(r'Width:\s+(\d+)', info)[1])
                            h = int(re.search(r'Height:\s+(\d+)', info)[1])
                            screen = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
                            assert 0 <= x < x+w <= screen.width and 0 <= y < y+h <= screen.height
                            image = screen.crop((x, y, x+w, y+h))
                            if not any(lo != hi for lo, hi in image.convert('RGB').getextrema()):
                                # X mapping precedes the first paint; keep the GUI gate mandatory.
                                continue
                            # A destroyed/replaced validator window is not valid capture evidence.
                            assert query('xprop', '-id', window, '_NET_WM_PID', 'WM_CLASS') == properties
                            assert 'Map State: IsViewable' in query('xwininfo', '-id', window)
                            image.save(args.output / 'host-editor.png')
                            screen.save(args.output / 'host-desktop.png')
                            (args.output / 'editor-window-tree.txt').write_text(tree)
                            (args.output / 'editor-window-properties.txt').write_text(properties + '\n' + info)
                            capture = dict(window=window, pid=pid, properties=properties, geometry=[x,y,w,h], loaded_module=str(module))
                        except (subprocess.CalledProcessError, FileNotFoundError):
                            # Validator-created editor windows are transient; wait for the live one.
                            continue
                        break
                time.sleep(.02)
            assert process.wait(timeout=10) == 0, log_path.read_text()
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    verify_validation(log_path.read_text())
    assert capture is not None, 'no actual attached editor captured during Editor Automation'
    receipt = dict(scope='infrastructure fixture only; NOT target UI/fidelity verification', host=version, command=command,
                   plugin=str(plugin), module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
                   host_sha256=hashlib.sha256(host.read_bytes()).hexdigest(), source_commit=os.environ.get('GITHUB_SHA'),
                   screenshot_sha256=hashlib.sha256((args.output/'host-editor.png').read_bytes()).hexdigest(),
                   capture=capture, review='actual attached editor; human control/layout review required')
    (args.output / 'editor-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == '__main__':
    main()
