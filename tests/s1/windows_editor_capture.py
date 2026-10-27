#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows capture of the actual Tsunami Stereo editor window (not the cropped desktop).

Opens the exact VST3 module's editor with Pedalboard ``show_editor``, finds the visible
window of *this* process whose client area is the fixed 1048x665 editor body, confirms the
exact module is loaded in-process, and grabs only that HWND with Pillow
screen pixels of that window's client rectangle. Native run 40519612875 (8de5a72) showed
``ImageGrab.grab(window=hwnd)`` (PrintWindow) returns a blank image for the JUCE peer, and
the 1024-wide runner desktop crops the 1048 body. This version therefore temporarily
switches the current desktop to a supported mode >=1280x800 (EnumDisplaySettingsW, then
ChangeDisplaySettingsExW with CDS_TEST, then non-persistent flags 0), reads the mode and
ImageGrab size back, grabs the client rectangle from the screen, and restores the registry
mode in ``finally``. It fails closed (never resizes, stitches or crops) and always writes
``capture-diagnostics.json`` plus any rejected image.
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


MIN_DESKTOP = (1280, 800)


def choose_display_mode(modes, current, minimum=MIN_DESKTOP):
    """Pure policy over mode records (dicts: width, height, bits, frequency, index).

    Returns None when the current desktop already fits; otherwise the smallest mode with at
    least ``minimum`` pixels at the current colour depth, preferring the current frequency.
    Raises LookupError when no supported mode is large enough (fail closed)."""
    if current['width'] >= minimum[0] and current['height'] >= minimum[1]:
        return None
    fits = [m for m in modes if m['width'] >= minimum[0] and m['height'] >= minimum[1] and m['bits'] == current['bits']]
    if not fits:
        raise LookupError(f'no supported display mode >= {minimum[0]}x{minimum[1]} at {current["bits"]} bpp')
    return min(fits, key=lambda m: (m['width'] * m['height'], m['frequency'] != current['frequency'], m['index']))


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


def _devmode():
    """DEVMODEW per wingdi.h (sizeof 220): the printer/display union is 16 bytes."""
    import ctypes
    from ctypes import wintypes

    class DEVMODEW(ctypes.Structure):
        _fields_ = [('dmDeviceName', wintypes.WCHAR * 32), ('dmSpecVersion', wintypes.WORD),
                    ('dmDriverVersion', wintypes.WORD), ('dmSize', wintypes.WORD), ('dmDriverExtra', wintypes.WORD),
                    ('dmFields', wintypes.DWORD), ('dmUnion', ctypes.c_short * 8), ('dmColor', ctypes.c_short),
                    ('dmDuplex', ctypes.c_short), ('dmYResolution', ctypes.c_short), ('dmTTOption', ctypes.c_short),
                    ('dmCollate', ctypes.c_short), ('dmFormName', wintypes.WCHAR * 32), ('dmLogPixels', wintypes.WORD),
                    ('dmBitsPerPel', wintypes.DWORD), ('dmPelsWidth', wintypes.DWORD), ('dmPelsHeight', wintypes.DWORD),
                    ('dmDisplayFlags', wintypes.DWORD), ('dmDisplayFrequency', wintypes.DWORD),
                    ('dmICMMethod', wintypes.DWORD), ('dmICMIntent', wintypes.DWORD), ('dmMediaType', wintypes.DWORD),
                    ('dmDitherType', wintypes.DWORD), ('dmReserved1', wintypes.DWORD), ('dmReserved2', wintypes.DWORD),
                    ('dmPanningWidth', wintypes.DWORD), ('dmPanningHeight', wintypes.DWORD)]
    assert ctypes.sizeof(DEVMODEW) == 220, ctypes.sizeof(DEVMODEW)
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.EnumDisplaySettingsW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(DEVMODEW)]
    user32.EnumDisplaySettingsW.restype = wintypes.BOOL
    user32.ChangeDisplaySettingsExW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(DEVMODEW), wintypes.HWND,
                                                wintypes.DWORD, wintypes.LPVOID]
    user32.ChangeDisplaySettingsExW.restype = wintypes.LONG
    return ctypes, DEVMODEW, user32


ENUM_CURRENT_SETTINGS = 0xFFFFFFFF
CDS_TEST = 0x2
DM_BITSPERPEL, DM_PELSWIDTH, DM_PELSHEIGHT, DM_DISPLAYFREQUENCY = 0x40000, 0x80000, 0x100000, 0x400000


def _mode_record(dm, index):
    return dict(index=index, width=dm.dmPelsWidth, height=dm.dmPelsHeight, bits=dm.dmBitsPerPel, frequency=dm.dmDisplayFrequency)


def display_modes():
    ctypes, DEVMODEW, user32 = _devmode()
    def read(index):
        dm = DEVMODEW(); dm.dmSize = ctypes.sizeof(DEVMODEW)
        return dm if user32.EnumDisplaySettingsW(None, index, ctypes.byref(dm)) else None
    current = _mode_record(read(ENUM_CURRENT_SETTINGS), -1)
    modes, i = [], 0
    while (dm := read(i)) is not None and i < 4096:
        modes.append(_mode_record(dm, i)); i += 1
    return current, modes


def apply_display_mode(mode, flags):
    """CDS_TEST (flags=2) or non-persistent current-desktop apply (flags=0); returns DISP_CHANGE_*."""
    ctypes, DEVMODEW, user32 = _devmode()
    dm = DEVMODEW(); dm.dmSize = ctypes.sizeof(DEVMODEW)
    dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency = mode['width'], mode['height'], mode['bits'], mode['frequency']
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY
    return int(user32.ChangeDisplaySettingsExW(None, ctypes.byref(dm), None, flags, None))


def restore_display_mode():
    ctypes, DEVMODEW, user32 = _devmode()
    return int(user32.ChangeDisplaySettingsExW(None, None, None, 0, None))  # back to registry mode


def client_screen_rect(hwnd):
    ctypes, wintypes, user32, _, _ = _win32()
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.ClientToScreen.restype = wintypes.BOOL
    rect, origin = wintypes.RECT(), wintypes.POINT(0, 0)
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return (origin.x, origin.y, origin.x + rect.right - rect.left, origin.y + rect.bottom - rect.top)


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
    diagnostics = dict(module=str(module), module_sha256=module_sha, pid=os.getpid(), errors=[], source_commit=os.environ.get('GITHUB_SHA'))
    changed = False
    try:
        current, modes = display_modes()
        diagnostics.update(initial_mode=current, mode_count=len(modes), desktop_before=list(ImageGrab.grab().size))
        target = choose_display_mode(modes, current)
        diagnostics['target_mode'] = target
        if target is not None:
            diagnostics['cds_test'] = apply_display_mode(target, CDS_TEST)
            assert diagnostics['cds_test'] == 0, f'CDS_TEST rejected mode: {diagnostics["cds_test"]}'
            diagnostics['cds_apply'] = apply_display_mode(target, 0)
            changed = diagnostics['cds_apply'] == 0
            assert changed, f'mode apply failed: {diagnostics["cds_apply"]}'
            time.sleep(2)
        diagnostics['mode_readback'], _ = display_modes()
        diagnostics['desktop_readback'] = list(ImageGrab.grab().size)
        assert diagnostics['desktop_readback'][0] >= MIN_DESKTOP[0] and diagnostics['desktop_readback'][1] >= MIN_DESKTOP[1], 'desktop readback too small'
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
                bbox = client_screen_rect(chosen['hwnd'])
                result.update(before=before, bbox=bbox, loaded_module=loaded)
                desktop = ImageGrab.grab()
                desktop.save(args.output / 'host-desktop-diagnostic.png')
                assert bbox[0] >= 0 and bbox[1] >= 0 and bbox[2] <= desktop.width and bbox[3] <= desktop.height, f'client {bbox} outside desktop {desktop.size}'
                image = ImageGrab.grab(bbox=bbox)
                after = select_editor_window(enumerate_windows(), os.getpid())
                result.update(after=after, size=image.size)
                if not (image.size == BODY and any(lo != hi for lo, hi in image.convert('RGB').getextrema())):
                    image.save(args.output / 'rejected-editor-capture.png')
                    raise AssertionError(f'rejected capture size {image.size} or blank')
                assert before['hwnd'] == after['hwnd'] == chosen['hwnd'], 'editor window changed during capture'
                image.save(args.output / 'host-editor-window.png')
            except BaseException as error:
                errors.append(repr(error))
            finally:
                close.set()

        thread = threading.Thread(target=capture)
        thread.start()
        plugin.show_editor(close)
        thread.join(timeout=40)
        diagnostics['capture'] = result
        assert not thread.is_alive() and not errors, errors
    except BaseException as error:
        diagnostics['errors'].append(repr(error))
        raise
    finally:
        if changed:
            diagnostics['restore'] = restore_display_mode()
        (args.output / 'capture-diagnostics.json').write_text(json.dumps(diagnostics, indent=2, default=str) + '\n')
    shot = args.output / 'host-editor-window.png'
    receipt = dict(scope='actual S1 module editor client rectangle grabbed from a temporarily enlarged, read-back desktop '
                         '(Pedalboard show_editor); docs/original-calibrated replacement UI, NOT original-host equivalence',
                   module=str(module), module_sha256=module_sha, loaded_module=result['loaded_module'],
                   window_before=result['before'], window_after=result['after'], client_bbox=list(result['bbox']),
                   image_size=list(result['size']), display=dict(initial=diagnostics['initial_mode'], target=diagnostics['target_mode'],
                   cds_test=diagnostics.get('cds_test'), cds_apply=diagnostics.get('cds_apply'), readback=diagnostics['mode_readback'],
                   desktop_readback=diagnostics['desktop_readback'], restore=diagnostics.get('restore')),
                   screenshot_sha256=hashlib.sha256(shot.read_bytes()).hexdigest(), pid=os.getpid(),
                   pedalboard=pedalboard.__version__, pillow=pillow_version, os=platform.platform(),
                   source_commit=os.environ.get('GITHUB_SHA'), prior_failure='run 40519612875: ImageGrab window=HWND blank')
    (args.output / 'windows-editor-receipt.json').write_text(json.dumps(receipt, indent=2, default=str) + '\n')
    print(json.dumps(receipt, indent=2, default=str))


if __name__ == '__main__':
    main()
