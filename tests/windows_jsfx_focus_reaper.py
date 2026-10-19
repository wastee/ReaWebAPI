"""Check FX-chain WebView focus transitions in an isolated Windows REAPER profile."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import shutil
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reaper', type=Path, required=True)
parser.add_argument('--extension', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cycles', type=int, default=10)
args = parser.parse_args()
if args.cycles < 1:
    parser.error('--cycles must be positive')
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / 'UserPlugins').mkdir()
(root / 'EmptyVST').mkdir()
effect = root / 'Effects/rwa-focus'
effect.mkdir(parents=True)
shutil.copy2(args.extension, root / 'UserPlugins/reaper_reawebapi-x64.dll')
for name in ('reaper-install-rev.txt', 'reaper-license.rk'):
    source = args.reaper.resolve().parent / name
    if source.is_file():
        shutil.copy2(source, root / name)
shutil.copy2(Path(__file__).resolve().parents[1] / 'runtime/jsfx-webui.mjs', effect)
(root / 'reaper.ini').write_text(
    '[REAPER]\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nvst_scan=0\nvstfullstate=49989\n'
    f'vstpath64={root / "EmptyVST"}\n'
    '[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\n'
    'waveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n')
(effect / 'probe.jsfx').write_text(
    'desc:ReaWeb focus regression\nslider1:0<0,1,0.01>Gain\n'
    'slider2:0<0,3,1{4,8,12,20}>Ratio\n@sample\nspl0*=slider1;spl1*=slider1;\n')
(effect / 'probe.json').write_text(json.dumps({
    'schema': 1, 'JSFX_WebUI': {'entry': 'index.html', 'width': 640, 'height': 300}}))
(effect / 'index.html').write_text('''<!doctype html><title>JSFX focus regression</title>
<input id="gain" type="range" min="0" max="1" step="0.01"><button id="ratio">Ratio</button>
<script type="module">
import {connectJSFX} from './jsfx-webui.mjs';
try {
  const jsfx=await connectJSFX(reaper);
  await jsfx.params.beginEdit(0);
  await jsfx.params.set(0,0.25);
  await jsfx.params.endEdit();
  await jsfx.params.set(1,2/3);
  document.getElementById('gain').onchange=event=>jsfx.params.set(0,Number(event.target.value));
  document.getElementById('ratio').onclick=()=>jsfx.params.set(1,1);
  await reaper.SetExtState('JSFXFocusRegression','ready','ready',false);
}catch(error){await reaper.SetExtState('JSFXFocusRegression','ready','error: '+error.message,false);}
</script>''', encoding='utf-8')
(root / 'launch.lua').write_text('''local root = ROOT
local function write(name,value)
  local file=assert(io.open(root..'/'..name,'w'));file:write(tostring(value));file:close()
end
local ok,err=pcall(function()
  assert(reaper.APIExists('ReaWeb_Open'),'ReaWebAPI did not initialize')
  reaper.InsertTrackAtIndex(0,false)
  local track=reaper.GetTrack(0,0)
  reaper.GetSetMediaTrackInfo_String(track,'P_NAME','JSFX focus regression (disposable)',true)
  local fx=reaper.TrackFX_AddByName(track,'rwa-focus/probe.jsfx',false,-1)
  assert(fx>=0,'Probe JSFX unavailable')
  local _,ident=reaper.TrackFX_GetNamedConfigParm(track,fx,'fx_ident')
  write('fixture.txt',ident)
  reaper.TrackFX_Show(track,fx,1)
  local function tick()
    local runtime_error=reaper.ReaWeb_GetLastError()
    if runtime_error~='' then write('runtime-error.txt',runtime_error) end
    local file=io.open(root..'/command.txt','r')
    if file then
      local request=file:read('*a');file:close();os.remove(root..'/command.txt')
      local id,op=request:match('^(%d+)|(.+)$')
      if id then
        local result='ok'
        if op=='ready' then result=reaper.GetExtState('JSFXFocusRegression','ready')
        elseif op=='params' then
          result=string.format('%.6f|%.6f',reaper.TrackFX_GetParamNormalized(track,fx,0),reaper.TrackFX_GetParamNormalized(track,fx,1))
        elseif op=='floating' then reaper.TrackFX_Show(track,fx,3)
        elseif op=='chain' then reaper.TrackFX_Show(track,fx,2);reaper.TrackFX_Show(track,fx,1)
        elseif op=='close' then reaper.TrackFX_Show(track,fx,2);reaper.TrackFX_Show(track,fx,0)
        end
        write('reply.txt',id..'|'..result)
      end
    end
    reaper.defer(tick)
  end
  tick()
end)
if not ok then write('error.txt',err) end
'''.replace('ROOT', json.dumps(root.as_posix())), encoding='utf-8')

u = C.WinDLL('user32', use_last_error=True)
k = C.WinDLL('kernel32')
u.GetForegroundWindow.restype = W.HWND
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.GetParent.argtypes = [W.HWND]
u.GetParent.restype = W.HWND
u.GetAncestor.argtypes = [W.HWND, W.UINT]
u.GetAncestor.restype = W.HWND
u.GetDlgItem.argtypes = [W.HWND, C.c_int]
u.GetDlgItem.restype = W.HWND
u.GetWindowLongW.argtypes = [W.HWND, C.c_int]
u.GetWindowLongW.restype = C.c_long
u.SetForegroundWindow.argtypes = [W.HWND]
u.ShowWindow.argtypes = [W.HWND, C.c_int]
u.BringWindowToTop.argtypes = [W.HWND]
u.SetFocus.argtypes = [W.HWND]
u.SetFocus.restype = W.HWND
u.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
u.GetGUIThreadInfo.argtypes = [W.DWORD, C.c_void_p]


def window_info(hwnd):
    pid = W.DWORD()
    thread = u.GetWindowThreadProcessId(hwnd, C.byref(pid))
    name = C.create_unicode_buffer(256)
    u.GetClassNameW(hwnd, name, len(name))
    return dict(hwnd=hwnd, pid=pid.value, thread=thread, cls=name.value,
                parent=u.GetParent(hwnd), exstyle=u.GetWindowLongW(hwnd, -20) & 0xffffffff)


def windows():
    result = []

    @C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def child(hwnd, _):
        result.append(window_info(hwnd))
        return True

    @C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def top(hwnd, _):
        if window_info(hwnd)['pid'] == process.pid:
            child(hwnd, 0)
            u.EnumChildWindows(W.HWND(hwnd), child, 0)
        return True

    u.EnumWindows(top, 0)
    return result


def wait(test, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (root / 'error.txt').exists():
            raise RuntimeError((root / 'error.txt').read_text())
        result = test()
        if result:
            return result
        time.sleep(.05)
    raise TimeoutError('REAPER did not complete the requested operation')


serial = 0


def command(operation):
    global serial
    serial += 1
    prefix = str(serial) + '|'
    pending = root / 'command.tmp'
    pending.write_text(prefix + operation)
    pending.replace(root / 'command.txt')

    def reply():
        try:
            value = (root / 'reply.txt').read_text()
            return value if value.startswith(prefix) else None
        except (FileNotFoundError, PermissionError):
            return None

    return wait(reply, 3).split('|', 1)[1]


def foreground(hwnd, thread, focus=None):
    current = k.GetCurrentThreadId()
    previous = u.GetWindowThreadProcessId(u.GetForegroundWindow(), None)
    attached_previous = u.AttachThreadInput(current, previous, True)
    attached_target = u.AttachThreadInput(current, thread, True)
    try:
        u.ShowWindow(hwnd, 9)
        u.BringWindowToTop(hwnd)
        u.SetForegroundWindow(hwnd)
        assert u.GetForegroundWindow() == hwnd, 'Cannot activate the test window'
        if focus:
            u.SetFocus(focus)
    finally:
        if attached_target:
            u.AttachThreadInput(current, thread, False)
        if attached_previous:
            u.AttachThreadInput(current, previous, False)


process = None
original_foreground = u.GetForegroundWindow()
report = dict(cycles=[], passed=False)
try:
    process = subprocess.Popen([str(args.reaper.resolve()), '-newinst', '-cfgfile',
                                str(root / 'reaper.ini'), '-new', '-nosplash', str(root / 'launch.lua')],
                               creationflags=subprocess.CREATE_NO_WINDOW)
    (root / 'process.pid').write_text(str(process.pid))
    native_chain = wait(lambda: next((w for w in windows()
                                     if w['cls'] == '#32770' and u.GetDlgItem(w['hwnd'], 1076)), None))
    foreground(native_chain['hwnd'], native_chain['thread'])
    wait(lambda: next((w for w in windows() if w['cls'] == 'ReaWebAPI.Window'), None))
    status = wait(lambda: command('ready'))
    assert status == 'ready', status
    assert command('params') == '0.250000|0.666667', 'Host parameter readback differs'
    for mode in ('chain', 'floating', 'chain'):
        command(mode)
        time.sleep(.3)
        for _ in range(args.cycles):
            tree = windows()
            web = next(w for w in tree if w['cls'] == 'ReaWebAPI.Window')
            browser = next(w for w in tree if w['cls'] == 'Chrome_WidgetWin_0' and w['parent'] == web['hwnd'])
            owner = u.GetAncestor(web['hwnd'], 2)
            main = next(w['hwnd'] for w in tree if w['cls'] == 'REAPERwnd')
            foreground(owner, web['thread'], browser['hwnd'])

            def browser_focus():
                gui = C.create_string_buffer(72)
                C.c_uint32.from_buffer(gui).value = 72
                if u.GetGUIThreadInfo(web['thread'], gui):
                    focus = C.c_void_p.from_buffer(gui, 16).value
                    return focus if focus and window_info(focus)['cls'] == 'Chrome_WidgetWin_1' else None

            focus = wait(browser_focus, 3)
            assert u.SetForegroundWindow(main), 'Cannot deactivate the test editor'
            command('ping')
            report['cycles'].append(dict(mode=mode, focus=hex(focus), responsive=True))
        if mode == 'chain':
            command('close')
            command('chain')
    assert command('params') == '0.250000|0.666667', 'Focus transitions changed parameters'
    report['passed'] = True
finally:
    (root / 'result.json').write_text(json.dumps(report, indent=2))
    if process and process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    (root / 'reaper-license.rk').unlink(missing_ok=True)
    if original_foreground:
        u.SetForegroundWindow(original_foreground)
print(json.dumps(report))
