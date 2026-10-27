#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows capture of the actual Tsunami Stereo editor window (not the cropped desktop).

Opens the exact VST3 module's editor with Pedalboard ``show_editor``, finds the visible
window of *this* process whose client area is the fixed 1048x665 editor body, confirms the
exact module is loaded in-process, and grabs only that HWND with Pillow
``ImageGrab.grab(window=hwnd)`` (supported since Pillow 11.2.1). Fails closed when no
unique editor window is found. The full desktop is saved separately for diagnostics only.
"""
import argparse
import hashlib
import json
import os
import platform
import threading
import time
from pathlib import Path

BODY = (1048, 665)


def select_editor_window(records, pid, body=BODY):
    """Pure selection over window records (dicts: hwnd, pid, visible, cls, child, client=(w, h)).

    Returns the unique best visible window of ``pid`` whose client area equals ``body``;
    top-level windows are preferred over children. Raises when there is no candidate or when
    the preferred level is ambiguous (more than one same-level match)."""
    candidates = [r for r in records if r['pid'] == pid and r['visible'] and tuple(r['client']) == tuple(body)]
    if not candidates:
        raise LookupError(f'no visible {body[0]}x{body[1]} editor window in process {pid}')
    for level in (False, True):
        same = [r for r in candidates if bool(r['child']) == level]
        if len(same) == 1:
            return same[0]
        if len(same) > 1:
            raise LookupError(f'ambiguous editor windows: {[hex(r["hwnd"]) for r in same]}')
    raise AssertionError('unreachable')


def _win32():
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [enum_proc, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.EnumChildWindows.argtypes = [wintypes.HWND, enum_proc, wintypes.LPARAM]
    user32.EnumChildWindows.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.restype = ctypes.c_int
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.GetClientRect.restype = wintypes.BOOL
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.GetWindowRect.restype = wintypes.BOOL
    kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
    kernel32.GetModuleHandleW.restype = wintypes.HMODULE
    kernel32.GetModuleFileNameW.argtypes = [wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    kernel32.GetModuleFileNameW.restype = wintypes.DWORD
    return ctypes, wintypes, user32, kernel32, enum_proc


def enumerate_windows():
    ctypes, wintypes, user32, _, enum_proc = _win32()
    records = []

    def describe(hwnd, child):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, name, 256)
        client, window = wintypes.RECT(), wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(client))
        user32.GetWindowRect(hwnd, ctypes.byref(window))
        records.append(dict(hwnd=int(hwnd), pid=pid.value, visible=bool(user32.IsWindowVisible(hwnd)), cls=name.value,
                            child=child, client=(client.right - client.left, client.bottom - client.top),
                            rect=(window.left, window.top, window.right, window.bottom)))

    @enum_proc
    def on_child(hwnd, _):
        describe(hwnd, True)
        return True

    @enum_proc
    def on_top(hwnd, _):
        describe(hwnd, False)
        user32.EnumChildWindows(hwnd, on_child, 0)
        return True

    user32.EnumWindows(on_top, 0)
    return records


def loaded_module_path(module):
    ctypes, wintypes, _, kernel32, _ = _win32()
    handle = kernel32.GetModuleHandleW(module.name)
    if not handle:
        return None
    buffer = ctypes.create_unicode_buffer(32768)
    length = kernel32.GetModuleFileNameW(handle, buffer, 32768)
    return buffer.value[:length] if length else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    assert platform.system() == 'Windows', 'native Windows only'
    from pedalboard import load_plugin
    import pedalboard
    from PIL import ImageGrab, __version__ as pillow_version
    bundle = args.plugin.resolve(strict=True)
    module = (bundle / 'Contents' / 'x86_64-win' / bundle.name).resolve(strict=True) if bundle.is_dir() else bundle
    module_sha = hashlib.sha256(module.read_bytes()).hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    plugin = load_plugin(str(module))
    close, result, errors = threading.Event(), {}, []

    def capture():
        try:
            deadline = time.monotonic() + 30
            chosen = None
            while chosen is None and time.monotonic() < deadline:
                time.sleep(.5)
                try:
                    chosen = select_editor_window(enumerate_windows(), os.getpid())
                except LookupError as error:
                    result['last_lookup'] = str(error)
            if chosen is None:
                raise LookupError(result.get('last_lookup', 'no editor window'))
            time.sleep(1.5)  # let the first paint complete
            loaded = loaded_module_path(module)
            assert loaded and Path(loaded).resolve() == module, f'exact module not loaded in-process: {loaded}'
            before = select_editor_window(enumerate_windows(), os.getpid())
            image = ImageGrab.grab(window=chosen['hwnd'])
            after = select_editor_window(enumerate_windows(), os.getpid())
            assert before['hwnd'] == after['hwnd'] == chosen['hwnd'], 'editor window changed during capture'
            assert image.width >= BODY[0] and image.height >= BODY[1], f'window image {image.size} smaller than body'
            assert any(lo != hi for lo, hi in image.convert('RGB').getextrema()), 'blank window capture'
            image.save(args.output / 'host-editor-window.png')
            ImageGrab.grab().save(args.output / 'host-desktop-diagnostic.png')
            result.update(before=before, after=after, size=image.size, loaded_module=loaded)
        except BaseException as error:
            errors.append(repr(error))
        finally:
            close.set()

    thread = threading.Thread(target=capture)
    thread.start()
    plugin.show_editor(close)
    thread.join(timeout=40)
    assert not thread.is_alive() and not errors, errors
    shot = args.output / 'host-editor-window.png'
    receipt = dict(scope='actual S1 module editor window capture (Pedalboard show_editor, Pillow ImageGrab window=HWND); '
                         'docs-derived original-style UI, NOT original-host equivalence',
                   module=str(module), module_sha256=module_sha, loaded_module=result['loaded_module'],
                   window_before=result['before'], window_after=result['after'], image_size=list(result['size']),
                   screenshot_sha256=hashlib.sha256(shot.read_bytes()).hexdigest(), pid=os.getpid(),
                   pedalboard=pedalboard.__version__, pillow=pillow_version, os=platform.platform(),
                   source_commit=os.environ.get('GITHUB_SHA'),
                   desktop_diagnostic='host-desktop-diagnostic.png (diagnostic only, may be cropped)')
    (args.output / 'windows-editor-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
