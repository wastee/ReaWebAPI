"""Real REAPER Docker divider dragging; run on a dedicated X11 display.

Build linux_resize first. Requires PyGObject, XTest and a REAPER executable
in REAWEB_TEST_REAPER. Never run this input test on a shared user display.
"""
import ctypes as C
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import gi
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk

assert os.environ.get('REAWEB_TEST_ISOLATED_DISPLAY') == '1', 'A dedicated X11 display is required'
root = Path(__file__).resolve().parents[1]
build = root / os.environ.get('REAWEB_TEST_BUILD_DIR', 'build-linux')
reaper = Path(os.environ['REAWEB_TEST_REAPER'])
xlib = C.CDLL('libX11.so.6')
xtest = C.CDLL('libXtst.so.6')
xlib.XOpenDisplay.argtypes = [C.c_char_p]; xlib.XOpenDisplay.restype = C.c_void_p
xlib.XFlush.argtypes = [C.c_void_p]
xlib.XSync.argtypes = [C.c_void_p, C.c_int]
xlib.XCloseDisplay.argtypes = [C.c_void_p]
xtest.XTestFakeMotionEvent.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_ulong]
xtest.XTestFakeButtonEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
display = xlib.XOpenDisplay(None)
assert display
screen = Gdk.get_default_root_window()
assert screen

def wait_for(predicate, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate(): return
        time.sleep(.03)
    raise AssertionError('Timed out waiting for the REAPER test host')

def move(x, y):
    xtest.XTestFakeMotionEvent(display, -1, int(x), int(y), 0)
    xlib.XFlush(display)

def button(down):
    xtest.XTestFakeButtonEvent(display, 1, int(down), 0)
    xlib.XFlush(display)

def sample(before, after, bitmap):
    left, top = max(before[0], after[0]), max(before[1], after[1])
    right, bottom = min(before[2], after[2]), min(before[3], after[3])
    assert right-left > 50 and bottom-top > 50
    data, channels, stride = bitmap.get_pixels(), bitmap.get_n_channels(), bitmap.get_rowstride()
    whites = 0
    for row in range(9):
        for col in range(17):
            x, y = left+5+(right-left-11)*col//16, top+5+(bottom-top-11)*row//8
            offset = y*stride+x*channels
            whites += min(data[offset:offset+3]) > 235
    green = 0
    for y in range(top+4, min(bottom, top+80), 4):
        for x in range(left+4, min(right, left+80), 4):
            offset = y*stride+x*channels
            r, g, b = data[offset:offset+3]
            green += g > 190 and g-r > 75 and g-b > 75
    return whites, green >= 4

try:
    for mode, position in [('top', 2), ('left', 1), ('bottom', 0), ('right', 3)]:
        profile = Path(tempfile.mkdtemp(prefix='linux-divider-'+mode+'-', dir=os.environ.get('REAWEB_TEST_PROFILE_DIR', str(root / '.cache'))))
        plugins = profile / 'UserPlugins'; plugins.mkdir()
        for name in ['reaper_reawebapi-x86_64.so', 'reawebapi-webview-x86_64', 'tests/reaper_resize_test.so']:
            shutil.copy2(build / name, plugins)
        (profile / 'resize-test.enabled').touch()
        (profile / 'reaper.ini').write_text('[reaper]\ndockermode0='+str(position)+
            '\ndockheight=300\ndockheight_t=300\ndockheight_l=420\ndockheight_r=420\ndockcompactsingle=1\n'
            'mixwnd_vis=0\nverchk=0\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nhasrecentlyopened=1\n'
            'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n')
        print('START', mode, profile, flush=True)
        with (profile / 'process.log').open('w') as log:
            process = subprocess.Popen([str(reaper), '-newinst', '-cfgfile', str(profile / 'reaper.ini'), '-new', '-nosplash'],
                stdout=log, stderr=log)
            try:
                def ready():
                    assert process.poll() is None, ('REAPER exited during startup', process.returncode, (profile / 'process.log').read_text())
                    return (profile / 'geometry.json').exists()
                wait_for(ready, 60)
                geometry = lambda: json.loads((profile / 'geometry.json').read_text())
                first = geometry()
                assert first['position'] == position and not first['floating'], first
                (profile / 'verify').touch()
                # Allow the host's evaluation countdown to finish and its dialog to close.
                def page_visible():
                    rect = geometry()['page']
                    bitmap = Gdk.pixbuf_get_from_window(screen, 0, 0, screen.get_width(), screen.get_height())
                    return sample(rect, rect, bitmap)[1]
                wait_for(page_visible, 20)
                wait_for(lambda: (profile / 'state.json').exists())
                initial_frames = json.loads((profile / 'state.json').read_text())['frames']
                l, t, r, b = first['docker']; x, y = (l+r)//2, (t+b)//2
                if position == 2: y = b-3
                elif position == 0: y = t+2
                elif position == 1: x = r-3
                else: x = l+2
                bitmap = Gdk.pixbuf_get_from_window(screen, 0, 0, screen.get_width(), screen.get_height())
                bitmap.savev(str(profile / 'initial.png'), 'png', [], [])
                move(x, y); time.sleep(.2); button(True); time.sleep(.05)
                records = []
                for frame in range(120):
                    offset = round(75*math.sin((frame+1)*2*math.pi/40))
                    move(x+offset if position % 2 else x, y if position % 2 else y+offset)
                    time.sleep(.025)
                    before = geometry()['page']
                    bitmap = Gdk.pixbuf_get_from_window(screen, 0, 0, screen.get_width(), screen.get_height())
                    after = geometry()['page']
                    whites, visible = sample(before, after, bitmap)
                    records.append({'frame': frame, 'rect': after, 'white': whites, 'content': visible})
                    if whites or not visible or frame in (0, 30, 60, 90, 119):
                        bitmap.savev(str(profile / ('frame-'+str(frame)+'.png')), 'png', [], [])
                button(False)
                (profile / 'frames.json').write_text(json.dumps(records, indent=2))
                sizes = [r['rect'][2]-r['rect'][0] if position % 2 else r['rect'][3]-r['rect'][1] for r in records]
                assert max(sizes)-min(sizes) > 100, ('Divider did not move', first, min(sizes), max(sizes))
                (profile / 'state.json').unlink(missing_ok=True)
                (profile / 'verify').touch()
                wait_for(lambda: (profile / 'state.json').exists())
                state = json.loads((profile / 'state.json').read_text())
                rect = geometry()['page']
                assert state['value'] == 'preserved' and state['frames'] > initial_frames+10, state
                assert abs(state['width']-(rect[2]-rect[0])) <= 1 and abs(state['height']-(rect[3]-rect[1])) <= 1, (state, rect)
                failures = sum(r['white'] > 0 for r in records)
                print(mode, 'captures=', len(records), 'white_frames=', failures, 'size_range=', [min(sizes), max(sizes)], state, flush=True)
                assert failures == 0, 'White pixels appeared while dragging the Docker divider'
                assert all(r['content'] for r in records), 'Page content disappeared during divider dragging'
            finally:
                button(False)
                bitmap = Gdk.pixbuf_get_from_window(screen, 0, 0, screen.get_width(), screen.get_height())
                if bitmap: bitmap.savev(str(profile / 'final.png'), 'png', [], [])
                (profile / 'close').touch()
                try: process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.terminate(); process.wait(timeout=5)
                if profile.parent != root / '.cache':
                    evidence = root / '.cache' / profile.name; evidence.mkdir(exist_ok=True)
                    for path in profile.iterdir():
                        if path.suffix in ('.log', '.png', '.json'): shutil.copy2(path, evidence / path.name)
    print('PASS: REAPER Docker divider pixels, viewport and page state', flush=True)
finally:
    xlib.XCloseDisplay(display)
