"""Exercise WebView typing with the pointer over Arrange View in an isolated REAPER.

Requires an unlocked Windows desktop. Restores the system pointer setting on exit.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import shutil
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--reaper', type=Path, required=True)
p.add_argument('--extension', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--baseline', action='store_true')
args = p.parse_args()
u = C.WinDLL('user32', use_last_error=True)
k = C.WinDLL('kernel32')
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = W.LPARAM
for name in ('GetForegroundWindow',):
    getattr(u, name).restype = W.HWND
u.SetForegroundWindow.argtypes = [W.HWND]
u.SetFocus.argtypes = [W.HWND]
u.BringWindowToTop.argtypes = [W.HWND]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.MoveWindow.argtypes = [W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.BOOL]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.WindowFromPoint.argtypes = [W.POINT]
u.WindowFromPoint.restype = W.HWND
class Cursor(C.Structure):
    _fields_ = [('size',W.DWORD),('flags',W.DWORD),('cursor',W.HANDLE),('point',W.POINT)]
class Gui(C.Structure):
    _fields_ = [('size',W.DWORD),('flags',W.DWORD)] + [(n,W.HWND) for n in ['active','focus','capture','menu','move','caret']] + [('rect',W.RECT)]
def text(hwnd, cls=False):
    buf = C.create_unicode_buffer(512)
    (u.GetClassNameW if cls else u.GetWindowTextW)(hwnd, buf, len(buf))
    return buf.value
def windows(parent=None):
    result=[]
    @C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    def visit(hwnd,_):
        pid=W.DWORD(); u.GetWindowThreadProcessId(hwnd,C.byref(pid))
        if pid.value==process.pid: result.append(hwnd)
        return True
    if parent: u.EnumChildWindows(W.HWND(parent),visit,0)
    else: u.EnumWindows(visit,0)
    return result
def wait(test, timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=test()
        if value: return value
        time.sleep(.05)
    raise RuntimeError('Timed out')
def status():
    ci=Cursor(); ci.size=C.sizeof(ci); assert u.GetCursorInfo(C.byref(ci))
    return {'visible':bool(ci.flags&1),'handle':ci.cursor,'x':ci.point.x,'y':ci.point.y}
saved=W.BOOL(); assert u.SystemParametersInfoW(0x1020,0,C.byref(saved),0)
position=W.POINT(); u.GetCursorPos(C.byref(position))
previous=u.GetForegroundWindow()
root=args.output.resolve(); root.mkdir(parents=True,exist_ok=False)
(root/'UserPlugins').mkdir(); shutil.copy2(args.extension,root/'UserPlugins'/args.extension.name)
(root/'EmptyVST').mkdir()
(root/'reaper.ini').write_text('[REAPER]\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nvstfullstate=49989\nvst_scan=0\nvstpath64='+str(root/'EmptyVST')+'\n[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n')
(root/'app.json').write_text('{"id":"cursor-acceptance"}')
(root/'index.html').write_text('<!doctype html><title>Cursor acceptance</title><input autofocus id="field"><script>field.oninput=()=>document.title="cursor-typed-"+field.value.length;</script>')
(root/'launch.lua').write_text("local root=debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\\\])')\nlocal ok,err=pcall(function() reaper.InsertTrackAtIndex(0,false); reaper.UpdateArrange(); assert(reaper.ReaWeb_Open(root..'index.html')>0,reaper.ReaWeb_GetLastError()) end)\nif not ok then local f=io.open(root..'error.txt','w'); f:write(tostring(err)); f:close() end")
process=None
try:
    assert u.SystemParametersInfoW(0x1021,0,C.c_void_p(1),2)
    process=subprocess.Popen([str(args.reaper.resolve()),'-newinst','-cfgfile',str(root/'reaper.ini'),'-new','-nosplash',str(root/'launch.lua')])
    page=wait(lambda:next((w for w in windows() if text(w)=='Cursor acceptance'),None))
    host=next(w for w in windows() if text(w,True)=='REAPERwnd')
    u.MoveWindow(host,20,20,1250,900,True); u.MoveWindow(page,720,80,550,420,True)
    thread=u.GetWindowThreadProcessId(page,None); foreground=u.GetWindowThreadProcessId(u.GetForegroundWindow(),None)
    attached=u.AttachThreadInput(k.GetCurrentThreadId(),foreground,True)
    target_attached=u.AttachThreadInput(k.GetCurrentThreadId(),thread,True)
    u.ShowWindow(W.HWND(page),9); u.BringWindowToTop(page); u.SetForegroundWindow(page); u.SetFocus(page)
    if target_attached: u.AttachThreadInput(k.GetCurrentThreadId(),thread,False)
    if attached: u.AttachThreadInput(k.GetCurrentThreadId(),foreground,False)
    wait(lambda:u.GetForegroundWindow()==page,3)
    children=[{'hwnd':w,'class':text(w,True),'text':text(w)} for w in windows(host)]
    (root/'windows.json').write_text(json.dumps(children,indent=2))
    arrange=next(w['hwnd'] for w in children if w['class']=='REAPERTrackListWindow')
    rect=W.RECT(); u.GetWindowRect(arrange,C.byref(rect))
    x,y=rect.left+80,rect.top+80
    assert u.SetCursorPos(x,y); time.sleep(.3)
    assert u.WindowFromPoint(W.POINT(x,y))==arrange
    before=status()
    gui=Gui(); gui.size=C.sizeof(gui); assert u.GetGUIThreadInfo(thread,C.byref(gui))
    assert gui.focus and text(gui.focus,True).startswith('Chrome_')
    u.keybd_event(0x41,0,0,0); u.keybd_event(0x41,0,2,0)
    wait(lambda:text(page).startswith('cursor-typed-')); time.sleep(.3)
    typed=status()
    moved=[]
    for i in range(1,4):
        assert u.SetCursorPos(x+10*i,y); time.sleep(.3); moved.append(status())
    cycles=[]
    if not args.baseline:
        for i in range(20):
            caption=text(page)
            u.keybd_event(0x41,0,0,0); u.keybd_event(0x41,0,2,0)
            wait(lambda:text(page)!=caption); time.sleep(.2)
            hidden=status()
            assert u.SetCursorPos(x+10*(i%2+1),y)
            start=time.monotonic()
            wait(lambda:status()['visible'],2)
            shown=status()
            current=Gui(); current.size=C.sizeof(current); assert u.GetGUIThreadInfo(thread,C.byref(current))
            cycles.append({'hidden':not hidden['visible'],'restored':shown['visible'],
                           'shapeRetained':shown['handle']==before['handle'],
                           'focusRetained':current.focus==gui.focus,
                           'restoreMs':round((time.monotonic()-start)*1000)})
    report={'baseline':args.baseline,'before':before,'typed':typed,'moved':moved,'caption':text(page),'cycles':cycles}
    report['passed']=before['visible'] and (not typed['visible'] and not any(v['visible'] for v in moved) if args.baseline else not typed['visible'] and all(v['visible'] for v in moved))
    report['passed'] = report['passed'] and all(c['hidden'] and c['restored'] and c['shapeRetained'] and c['focusRetained'] for c in cycles)
    (root/'result.json').write_text(json.dumps(report,indent=2)); print(json.dumps(report),flush=True)
    assert report['passed']
finally:
    if process:
        process.terminate(); process.wait(timeout=10)
    u.SystemParametersInfoW(0x1021,0,C.c_void_p(saved.value),2)
    u.SetCursorPos(position.x,position.y)
    u.SetForegroundWindow(previous)
