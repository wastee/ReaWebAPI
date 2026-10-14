"""Cold-start pixel acceptance. Requires linux_startup and an isolated X11 REAPER display."""
import ctypes as C
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import gi
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk

assert os.environ.get('REAWEB_TEST_ISOLATED_DISPLAY') == '1', 'A dedicated X11 display is required'
root = Path(__file__).resolve().parents[1]
build = root / os.environ.get('REAWEB_TEST_BUILD_DIR', 'build-linux')
x = C.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [C.c_char_p]; x.XOpenDisplay.restype = C.c_void_p
x.XDefaultRootWindow.argtypes = [C.c_void_p]; x.XDefaultRootWindow.restype = C.c_ulong
x.XQueryTree.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_ulong), C.POINTER(C.POINTER(C.c_ulong)), C.POINTER(C.c_uint)]
x.XFetchName.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_char_p)]
x.XFree.argtypes = [C.c_void_p]
x.XCloseDisplay.argtypes = [C.c_void_p]
class WindowAttributes(C.Structure):
    _fields_ = [(n,C.c_int) for n in ('x','y','width','height','border_width','depth')] + [
        ('visual',C.c_void_p),('root',C.c_ulong),('window_class',C.c_int),('bit_gravity',C.c_int),('win_gravity',C.c_int),
        ('backing_store',C.c_int),('backing_planes',C.c_ulong),('backing_pixel',C.c_ulong),('save_under',C.c_int),
        ('colormap',C.c_ulong),('map_installed',C.c_int),('map_state',C.c_int),('all_event_masks',C.c_long),
        ('your_event_mask',C.c_long),('do_not_propagate_mask',C.c_long),('override_redirect',C.c_int),('screen',C.c_void_p)]
x.XGetWindowAttributes.argtypes = [C.c_void_p,C.c_ulong,C.POINTER(WindowAttributes)]
x.XTranslateCoordinates.argtypes = [C.c_void_p,C.c_ulong,C.c_ulong,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_ulong)]
display = x.XOpenDisplay(None); assert display
screen = Gdk.get_default_root_window(); assert screen
def native_origin(window):
    left,top,child=C.c_int(),C.c_int(),C.c_ulong()
    x.XTranslateCoordinates(display,window,x.XDefaultRootWindow(display),0,0,C.byref(left),C.byref(top),C.byref(child))
    return left.value,top.value
def exposed_at(window, point):
    root_window = x.XDefaultRootWindow(display)
    parent = root_window
    while parent:
        if parent == window: return True
        left, top, child = C.c_int(), C.c_int(), C.c_ulong()
        x.XTranslateCoordinates(display,root_window,parent,*point,C.byref(left),C.byref(top),C.byref(child))
        parent = child.value
    return False
def find_window(window, depth=0):
    name = C.c_char_p()
    if x.XFetchName(display, window, C.byref(name)) and name.value:
        text = name.value.decode('utf-8', 'replace'); x.XFree(name)
        if text == 'Startup acceptance' or text.startswith('ReaWebAPI — startup-test') or text.startswith('ReaWebAPI — Scripts'):
            return window
    if depth >= 3: return None
    base, parent, count, children = C.c_ulong(), C.c_ulong(), C.c_uint(), C.POINTER(C.c_ulong)()
    if not x.XQueryTree(display, window, C.byref(base), C.byref(parent), C.byref(children), C.byref(count)): return None
    ids = list(children[:count.value]); x.XFree(children)
    for child in ids:
        found = find_window(child, depth+1)
        if found: return found
    return None
class Page(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/style.css':
            time.sleep(.4)
            body = b'html{background:#182838}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}'; kind = 'text/css'
        else:
            body = b'<!doctype html><title>Startup acceptance</title><link rel="stylesheet" href="/style.css"><div id="marker"></div>'; kind = 'text/html'
        self.send_response(200); self.send_header('Content-Type',kind); self.send_header('Cache-Control','no-store'); self.end_headers(); self.wfile.write(body)
    def log_message(self,*args): pass
server = ThreadingHTTPServer(('127.0.0.1',0),Page)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    for repeat in range(int(os.environ.get('STARTUP_RUNS','20'))):
        for variant in range(4):
            profile = Path(tempfile.mkdtemp(prefix=f'startup-{variant}-{repeat}-',dir=os.environ.get('REAWEB_TEST_PROFILE_DIR', str(root/'.cache'))))
            plugins = profile / 'UserPlugins'; plugins.mkdir()
            for name in ['reaper_reawebapi-x86_64.so','reawebapi-webview-x86_64','tests/reaper_startup_test.so']:
                shutil.copy2(build/name,plugins)
            (profile/'startup-test.enabled').write_text(str(variant))
            (profile/'startup-test.url').write_text(f'http://127.0.0.1:{server.server_port}/')
            (profile/'reaper.ini').write_text('[reaper]\nmixwnd_vis=0\nverchk=0\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nhasrecentlyopened=1\nlinux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n')
            print('START',repeat,variant,profile,flush=True)
            with (profile/'process.log').open('w') as log:
                process=subprocess.Popen([os.environ['REAWEB_TEST_REAPER'],'-newinst','-cfgfile',str(profile/'reaper.ini'),'-new','-nosplash'],stdout=log,stderr=log)
                samples = 0; started = time.monotonic()
                try:
                    while samples < 20:
                        assert time.monotonic()-started < 25, 'First-visible-frame timeout'
                        xid = find_window(x.XDefaultRootWindow(display))
                        if xid:
                            geometry=WindowAttributes(); x.XGetWindowAttributes(display,xid,C.byref(geometry))
                            origin=native_origin(xid)
                            if geometry.map_state == 2 and geometry.width > 180 and geometry.height > 180:
                                # Mapped windows may still be behind REAPER while the WM places them.
                                point = (origin[0]+30, origin[1]+30)
                                if not exposed_at(xid, point): continue
                                bitmap = Gdk.pixbuf_get_from_window(screen,origin[0],origin[1],geometry.width,geometry.height)
                                if native_origin(xid) != origin or not exposed_at(xid, point): continue
                                pixels, stride, channels = bitmap.get_pixels(),bitmap.get_rowstride(),bitmap.get_n_channels()
                                def pixel(px,py):
                                    offset=py*stride+px*channels; return pixels[offset:offset+3]
                                r,g,b=pixel(30,30); background=pixel(160,160)
                                if not (g > 190 and g-r > 75 and g-b > 75 and min(background) < 240):
                                    bitmap.savev(str(profile/'startup-failure.png'),'png',[],[])
                                    bitmap.savev(str(root/'.cache/startup-linux-failure.png'),'png',[],[])
                                    raise AssertionError(('Blank initial native frame',list(pixel(30,30)),list(background),str(profile)))
                                samples += 1
                        time.sleep(.005)
                    print('PASS:',repeat,variant,'samples=',samples,flush=True)
                finally:
                    (profile/'close').touch()
                    try: process.wait(timeout=15)
                    except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=10)
finally:
    server.shutdown(); x.XCloseDisplay(display)
