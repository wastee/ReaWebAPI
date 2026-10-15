"""Real REAPER desktop pixel acceptance. Requires an isolated composited X11 display."""
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import Gtk, Gdk, GdkX11

assert os.environ.get('REAWEB_TEST_ISOLATED_DISPLAY') == '1'
root = Path(__file__).resolve().parents[1]
build = root / os.environ.get('REAWEB_TEST_BUILD_DIR', 'build-linux')
screen = Gdk.get_default_root_window()
xlib = C.CDLL('libX11.so.6')
xlib.XOpenDisplay.restype = C.c_void_p
xlib.XOpenDisplay.argtypes = [C.c_char_p]
xlib.XRaiseWindow.argtypes = [C.c_void_p, C.c_ulong]
xlib.XFlush.argtypes = [C.c_void_p]
xlib.XFetchName.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_void_p)]
xlib.XFree.argtypes = [C.c_void_p]
xtest = C.CDLL('libXtst.so.6')
xtest.XTestFakeMotionEvent.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_ulong]
xtest.XTestFakeButtonEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
display = xlib.XOpenDisplay(None)
background = Gtk.Window(title='Alpha background')
background.set_decorated(False)
background.set_default_size(1440, 1000)
background.move(0, 0)
color = [20/255, 80/255, 180/255]
def paint_background():
    rgba = Gdk.RGBA(*color, 1)
    background.override_background_color(Gtk.StateFlags.NORMAL, rgba)
paint_background()
background.show_all()

def pump(seconds=.1):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        while Gtk.events_pending(): Gtk.main_iteration_do(False)
        time.sleep(.005)

def wait_file(path, seconds=25):
    deadline = time.monotonic()+seconds
    while not path.exists() and time.monotonic() < deadline: pump()
    assert path.exists(), f'Timed out: {path}'

for frameless in (True, False):
    profile = Path(tempfile.mkdtemp(prefix='transparent-pixels-', dir=os.environ.get('REAWEB_TEST_PROFILE_DIR', str(root/'.cache'))))
    plugins = profile/'UserPlugins'; plugins.mkdir()
    for name in ('reaper_reawebapi-x86_64.so', 'reawebapi-webview-x86_64'):
        shutil.copy2(build/name, plugins/name)
    app = profile/'Scripts/Alpha'; app.mkdir(parents=True)
    (app/'app.json').write_text('{"id":"transparent-pixel-acceptance"}')
    (app/'index.html').write_text('''<!doctype html><title>Alpha REAPER acceptance</title>
<style>html,body{background:transparent;margin:0}div{position:absolute;left:100px;top:100px;width:200px;height:160px;background:rgba(255,0,0,.5);border-radius:20px;box-shadow:0 8px 15px #0008}</style><div></div>
<script>reaper.lifecycle.ready.then(async()=>{await reaper.window.setSize(600,420); document.addEventListener('mousedown',()=>reaper.host.send('click')); await reaper.host.send('ready');});</script>''')
    (profile/'reaper.ini').write_text('[reaper]\nmixwnd_vis=0\nverchk=0\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nhasrecentlyopened=1\nlinux_audio_mode=3\n')
    options = json.dumps(dict(transparent=True, frameless=frameless, hidden=True))
    script = profile/'test.lua'
    script.write_text(f'''local root = {json.dumps(str(profile))}
local id = reaper.ReaWeb_OpenEx(root .. '/Scripts/Alpha/index.html', {json.dumps(options)})
local function save(name, text) local f=assert(io.open(root..'/'..name,'w')); f:write(text); f:close() end
if id == 0 then save('ready', 'ERROR:'..reaper.ReaWeb_GetLastError()); return end
local function step()
  local message = reaper.ReaWeb_Receive(id)
  if message == 'ready' then reaper.ReaWeb_Show(id); reaper.ReaWeb_Focus(id); save('ready','ok') end
  if message == 'click' then save('click','ok') end
  local f=io.open(root..'/finish','r')
  if f then f:close(); reaper.ReaWeb_Close(id); reaper.Main_OnCommand(40004,0); return end
  reaper.defer(step)
end
step()
''')
    with (profile/'process.log').open('w') as log:
        process = subprocess.Popen([os.environ['REAWEB_TEST_REAPER'], '-newinst', '-cfgfile', str(profile/'reaper.ini'), '-new', '-nosplash', str(script)], stdout=log, stderr=log)
        try:
            wait_file(profile/'ready')
            if os.environ.get('REAWEB_TEST_NO_COMPOSITOR') == '1':
                assert 'composit' in (profile/'ready').read_text(), (profile/'ready').read_text()
                print('PASS: no-compositor creation rejected synchronously', flush=True)
                continue
            assert (profile/'ready').read_text() == 'ok', (profile/'ready').read_text()
            pump(1)
            windows = Gdk.Screen.get_default().get_window_stack()
            def title(window):
                value = C.c_void_p()
                xlib.XFetchName(display, window.get_xid(), C.byref(value))
                result = C.string_at(value).decode() if value.value else ''
                if value.value: xlib.XFree(value)
                return result
            native = next(w for w in windows if title(w) == 'Alpha REAPER acceptance')
            assert native.get_visual().get_depth() == 32
            xlib.XRaiseWindow(display, background.get_window().get_xid())
            xlib.XRaiseWindow(display, native.get_xid()); xlib.XFlush(display)
            pump(.5)
            x, y = native.get_origin()[-2:]
            before = None
            for rgb in ((20, 80, 180), (80, 160, 40)):
                color[:] = [v/255 for v in rgb]; paint_background(); pump(.25)
                bitmap = Gdk.pixbuf_get_from_window(screen, x, y, native.get_width(), native.get_height())
                bitmap.savev(str(profile/f'desktop-{rgb[0]}.png'), 'png', [], [])
                pixels = bitmap.get_pixels(); stride=bitmap.get_rowstride(); channels=bitmap.get_n_channels()
                def pixel(px,py): return tuple(pixels[py*stride+px*channels:py*stride+px*channels+3])
                clear, half = pixel(40,40), pixel(160,160)
                print('framed=',not frameless,'clear=',clear,'half=',half,'expected=',rgb,flush=True)
                assert all(abs(a-b)<=3 for a,b in zip(clear,rgb)), (clear,rgb,profile)
                if before:
                    assert all(abs((half[i]-before[i])-(rgb[i]-(20,80,180)[i])/2)<=3 for i in range(3)), (before,half,profile)
                before=half
            xtest.XTestFakeMotionEvent(display, -1, x+40, y+40, 0)
            xtest.XTestFakeButtonEvent(display, 1, 1, 0)
            xtest.XTestFakeButtonEvent(display, 1, 0, 0)
            xlib.XFlush(display)
            wait_file(profile/'click')
            (profile/'finish').touch()
            process.wait(timeout=10)
            print('PASS: real REAPER RGBA host, desktop Alpha and rectangular input, framed='+str(not frameless),flush=True)
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=5)
background.destroy()
