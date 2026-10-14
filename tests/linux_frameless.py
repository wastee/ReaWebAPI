"""Opt-in WebKitGTK native input acceptance on a dedicated EWMH X11 display."""
import ctypes as C
import json
import os
from pathlib import Path
import select
import socket
import tempfile
import time
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import Gtk, Gdk, GdkX11

assert os.environ.get('REAWEB_TEST_ISOLATED_DISPLAY') == '1', 'Use a dedicated display'
root = Path(__file__).resolve().parents[1]
build = root / os.environ.get('REAWEB_TEST_BUILD_DIR', 'build-linux')
fixture = Path(tempfile.mkdtemp(prefix='reaweb-frameless-'))
page = fixture / 'index.html'
page.write_text('''<!doctype html><style>html{background:#182838;color:white}body{margin:0}header{height:60px;padding:16px;box-sizing:border-box}button{float:right}</style>
<header data-reaweb-drag>Native title <button onclick="++window.clicks">Button</button></header><input value="Editable"><script>window.clicks=0;window.marker=731;</script>''')
host = Gtk.Window(title='Frameless helper acceptance')
host.set_decorated(False)
host.set_default_size(600, 400)
host.move(200, 180)
host.add(Gtk.DrawingArea())
host.show_all()
parent, child = socket.socketpair()
helper = str(build / 'reawebapi-webview-x86_64')
process = os.posix_spawn(helper, [helper, str(fixture / 'profile')], dict(os.environ),
    file_actions=[(os.POSIX_SPAWN_DUP2, child.fileno(), 3), (os.POSIX_SPAWN_CLOSE, child.fileno())])
child.close()
parent.setblocking(False)
buffer, messages = b'', []
xlib = C.CDLL('libX11.so.6')
xtest = C.CDLL('libXtst.so.6')
xlib.XOpenDisplay.argtypes = [C.c_char_p]; xlib.XOpenDisplay.restype = C.c_void_p
xlib.XFlush.argtypes = [C.c_void_p]; xlib.XCloseDisplay.argtypes = [C.c_void_p]
xtest.XTestFakeMotionEvent.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_ulong]
xtest.XTestFakeButtonEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
display = xlib.XOpenDisplay(None)
assert display

def send(**message): parent.sendall((json.dumps(message) + '\n').encode())
def pump(duration=.1):
    global buffer
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        while Gtk.events_pending(): Gtk.main_iteration_do(False)
        if select.select([parent], [], [], .005)[0]:
            data = parent.recv(65536); assert data, 'Helper exited'
            buffer += data
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                message = json.loads(line); messages.append(message)
                assert message['op'] != 'error', message
def geometry():
    native = host.get_window()
    return (*native.get_origin()[-2:], native.get_width(), native.get_height())
def sync():
    _, _, w, h = geometry()
    send(id=1, op='geometry', parent=host.get_window().get_xid(), x=0, y=0, width=w, height=h, visible=True, docked=False)
def move(x, y):
    xtest.XTestFakeMotionEvent(display, -1, int(x), int(y), 0); xlib.XFlush(display); pump(.04)
def button(down):
    xtest.XTestFakeButtonEvent(display, 1, int(down), 0); xlib.XFlush(display); pump(.08)
def gesture(x, y, dx, dy):
    move(x, y); button(True)
    for n in range(1, 11): move(x + dx*n/10, y + dy*n/10)
    button(False); pump(.2); sync(); pump(.15)
def report():
    token = str(time.monotonic_ns())
    send(id=1, op='eval', script=f'webkit.messageHandlers.reaweb.postMessage(JSON.stringify({{token:"{token}",marker,clicks}}))')
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        pump(.02)
        for m in messages:
            if m['op'] == 'message':
                value = json.loads(m['message'])
                if value.get('token') == token: return value
    raise AssertionError('No report')

try:
    pump(.4)
    assert any(m.get('frameless') for m in messages if m['op'] == 'ready'), messages
    for resizable in (True, False):
        host.set_resizable(resizable)
        send(id=1, op='open', uri=page.as_uri(), frameless=True, resizable=resizable,
             script='window.reaper={events:{on(){}},window:{getState:()=>Promise.resolve({framelessActive:true})}};')
        sync(); pump(1.5)
        x, y, w, h = geometry()
        gesture(x+150, y+28, 60, 35)
        moved = geometry()
        assert moved[:2] == (x+60, y+35), ('drag', (x,y,w,h), moved)
        for horizontal, vertical in ((0,1),(2,1),(1,0),(1,2),(0,0),(2,0),(0,2),(2,2)):
            host.resize(600,400); host.move(200,180); pump(.2); sync(); pump(.2)
            x,y,w,h = geometry()
            px = x+2 if horizontal==0 else x+w-2 if horizontal==2 else x+w//2
            py = y+2 if vertical==0 else y+h-2 if vertical==2 else y+h//2
            gesture(px,py,0 if horizontal==1 else 30,0 if vertical==1 else 30)
            after = geometry()
            assert (after[2:] != (w,h)) == resizable, ('resize',horizontal,vertical,(x,y,w,h),after)
        host.resize(600,400); host.move(200,180); pump(.2); sync(); pump(.2)
        x,y,w,h = geometry()
        move(x+w-45,y+26); button(True); button(False)
        value = report()
        assert value['marker'] == 731 and value['clicks'] == 1, value
        send(id=1, op='park'); pump(.1)
        sync(); pump(.3)
        assert report()['marker']==731
        send(id=1,op='close'); pump(.2)
        print('PASS: native drag, eight resize edges, exclusions, park/restore, resizable='+str(resizable), flush=True)
finally:
    parent.close()
    os.waitpid(process,0)
    host.destroy()
    xlib.XCloseDisplay(display)
