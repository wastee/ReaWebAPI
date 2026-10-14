"""Hidden startup and Lua/JS lifecycle acceptance in an isolated REAPER profile."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import time

PAGE = r'''<!doctype html><meta charset="utf-8"><title>Hidden startup acceptance</title>
<style>html{background:#182838;color:white}</style><p>Hidden startup acceptance</p>
<script>
(async () => {
  await reaper.lifecycle.ready;
  await reaper.events.on('message', async command => {
    try {
      if (command === 'reload') { await reaper.window.reload(); return; }
      if (command === 'hide') await reaper.window.hide();
      if (command === 'dock') await reaper.window.setDocked(true);
      if (command === 'undock') await reaper.window.setDocked(false);
      const state = await reaper.window.getState();
      await reaper.host.send(command + ':' + state.visible + ':' + state.focused + ':' + state.docked);
    } catch (error) { await reaper.host.send('ERROR:' + error.message); }
  });
  const state = await reaper.window.getState();
  await reaper.host.send('ready:' + state.visible + ':' + state.focused + ':' + state.docked);
})();
</script>'''

SCRIPT = r'''
local root = debug.getinfo(1, 'S').source:sub(2):match('^(.*[/\\])')
local page = root .. 'Scripts/Hidden/index.html'
local id, run, stage, deadline, hold = 0, 0, 'open', reaper.time_precise() + 30, 0
local options = '{"instanceKey":"hidden-acceptance","id":"main","hidden":true}'
local function check(ok, message) if not ok then error(message or reaper.ReaWeb_GetLastError()) end end
local function finish(text)
  if id > 0 then reaper.ReaWeb_Close(id) end
  local f = assert(io.open(root .. 'result.txt', 'w')); f:write(text); f:close()
  reaper.Main_OnCommand(40004, 0)
end
local function command(text, nextstage)
  check(reaper.ReaWeb_Send(id, text)); stage = nextstage; deadline = reaper.time_precise() + 30
end
local function step()
  check(reaper.time_precise() < deadline, 'Timeout at ' .. stage .. ' run ' .. run)
  if stage == 'open' then
    run = run + 1
    id = reaper.ReaWeb_OpenEx(page, options); check(id > 0)
    check(reaper.ReaWeb_OpenEx('missing.html', options) == id)
    stage = 'ready'
  end
  local message = reaper.ReaWeb_Receive(id)
  check(not message:match('^ERROR:'), message)
  if message ~= '' then
    local f = assert(io.open(root .. 'states.log', 'a')); f:write(run .. ' ' .. stage .. ' ' .. message .. '\n'); f:close()
  end
  if stage == 'ready' and message:match('^ready:') then
    check(reaper.ReaWeb_IsReady(id)); check(message:match('^ready:false:false:'), message)
    hold = reaper.time_precise() + (run == 1 and HOLD_SECONDS or 0)
    stage = 'hold'; deadline = hold + 30
  elseif stage == 'hold' and reaper.time_precise() >= hold then command('ping', 'ping')
  elseif stage == 'ping' and message:match('^ping:') then
    check(message:match('^ping:false:false:'), message)
    check(reaper.ReaWeb_Show(id)); command('shown', 'shown')
  elseif stage == 'shown' and message:match('^shown:') then
    check(message:match('^shown:true:'), message)
    check(reaper.ReaWeb_OpenEx('missing.html', options) == id)
    check(reaper.ReaWeb_Focus(id)); command('focused', 'focused')
  elseif stage == 'focused' and message:match('^focused:') then
    check(message:match('^focused:true:'), message)
    if message:match('^focused:true:false:') then
      check(reaper.ReaWeb_Send(id, 'focused'))
    else command(run % 2 == 1 and 'dock' or 'undock', 'dock') end
  elseif stage == 'dock' and (message:match('^dock:') or message:match('^undock:')) then command('hide', 'hidden')
  elseif stage == 'hidden' and message:match('^hide:') then
    check(message:match('^hide:false:'), message); command('reload', 'reloaded')
  elseif stage == 'reloaded' and message:match('^ready:') then
    check(message:match('^ready:false:'), message)
    check(reaper.ReaWeb_Open(page, 'hidden-acceptance', 'main') == id)
    command('reused', 'reused')
  elseif stage == 'reused' and message:match('^reused:') then
    check(message:match('^reused:true:'), message)
    if message:match('^reused:true:false:') then
      check(reaper.ReaWeb_Send(id, 'reused'))
    else check(reaper.ReaWeb_Close(id)); stage = 'closed' end
  elseif stage == 'closed' and not reaper.ReaWeb_IsOpen(id) then
    check(not reaper.ReaWeb_Show(id)); check(not reaper.ReaWeb_Focus(id))
    if run >= RUN_COUNT then finish('PASS: ' .. run .. ' hidden opens, ready, messages, Show/Focus, reuse, Dock restore, reload and close'); return end
    id = 0; stage = 'open'; deadline = reaper.time_precise() + 30
  end
  reaper.defer(function() local ok, err = pcall(step); if not ok then finish('FAIL: ' .. tostring(err)) end end)
end
local ok, err = pcall(step); if not ok then finish('FAIL: ' .. tostring(err)) end
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reaper', type=Path, required=True)
    parser.add_argument('--extension', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--count', type=int, default=20)
    parser.add_argument('--hold-seconds', type=int, default=0)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / 'UserPlugins').mkdir()
    (root / 'EmptyVST').mkdir()
    (root / 'reaper.ini').write_text('[REAPER]\naudiomode=4\nloadlastproj=0\nshowlastproj=0\nsplash=0\nnewprojdo=0\nverchk=0\nerrnowarn=5\nmixwnd_vis=0\nhasrecentlyopened=1\nvstfullstate=49989\nvst_scan=0\nvstpath64=' + str(root / 'EmptyVST') + '\n[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n')
    target = root / 'UserPlugins' / args.extension.name
    shutil.copy2(args.extension, target)
    if sys.platform == 'darwin':
        config = root / 'reaper.ini'
        config.write_text(config.read_text().replace('[REAPER]', '[reaper]').replace('[audioconfig]\nmode=4', '[audioconfig]\nmode=0'))
        subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
    elif sys.platform.startswith('linux'):
        for helper in args.extension.parent.glob('reawebapi-webview-*'):
            shutil.copy2(helper, root / 'UserPlugins' / helper.name)
    app = root / 'Scripts/Hidden'
    app.mkdir(parents=True)
    (app / 'index.html').write_text(PAGE, encoding='utf-8')
    (app / 'app.json').write_text('{"id":"hidden-acceptance"}')
    script = root / 'test.lua'
    script.write_text(SCRIPT.replace('RUN_COUNT', str(args.count)).replace('HOLD_SECONDS', str(args.hold_seconds)), encoding='utf-8')
    with (root / 'process.log').open('w') as log:
        process = subprocess.Popen([str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(script)], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + args.count * 40 + args.hold_seconds + 40
            while not (root / 'result.txt').exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.2)
            result = (root / 'result.txt').read_text() if (root / 'result.txt').exists() else 'FAIL: no result'
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
    print(result, flush=True)
    return 0 if result.startswith('PASS:') else 1


if __name__ == '__main__':
    raise SystemExit(main())
