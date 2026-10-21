"""Sample 1175 startup and instance isolation in a disposable REAPER profile."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
for name in ('reaper', 'extension', 'effect', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--sdk', type=Path, default=Path(__file__).resolve().parents[1] / 'runtime/jsfx-webui.mjs')
parser.add_argument('--idle-webviews', type=int, choices=range(33), help='Override the idle container target in the disposable profile')
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
if args.idle_webviews is not None:
    (root / 'ReaWebAPI.ini').write_text(f'[ReaWebAPI]\nIdleWebViews={args.idle_webviews}\n', encoding='utf-8')
(root / 'UserPlugins').mkdir()
shutil.copy2(args.extension, root / 'UserPlugins' / args.extension.name)
if sys.platform == 'darwin':
    subprocess.run(['codesign', '--force', '--sign', '-', str(root / 'UserPlugins' / args.extension.name)], check=True)
if sys.platform.startswith('linux'):
    helper = args.extension.parent / ('reawebapi-webview-' + args.extension.stem.split('reaper_reawebapi-', 1)[1])
    shutil.copy2(helper, root / 'UserPlugins' / helper.name)
    (root / 'UserPlugins' / helper.name).chmod(0o755)
effect = root / 'Effects/startup-1175'
shutil.copytree(args.effect, effect)
shutil.copy2(args.sdk, effect / '.webui/jsfx-webui.mjs')
page = effect / '.webui/index.html'
page.write_text(page.read_text(encoding='utf-8') + '\n<script type="module" src="./startup-observer.js"></script>', encoding='utf-8')
(effect / '.webui/startup-observer.js').write_text('''
await reaper.lifecycle.ready;
let busy=false;
setInterval(async()=>{
  if(busy)return;busy=true;
  try {
    const d=await reaper.debug.getDiagnostics();
    if(!d.jsfx?.presented||d.startup.milestones.revealCompleteMs===undefined)return;
    const state=await reaper.host.service('jsfx').invoke('getParamState',{sessionId:d.startup.sessionId});
    if(!state.ok)throw new Error(JSON.stringify(state.error));
    const token=await reaper.GetExtState('StartupProbe','token');
    const native=state.data.parameters[0].normalized;
    const hex=d.jsfx.fxGuid.map(x=>x.toString(16).padStart(2,'0').toUpperCase());
    const guid='{'+[[3,2,1,0],[5,4],[7,6],[8,9],[10,11,12,13,14,15]].map(group=>group.map(i=>hex[i]).join('')).join('-')+'}';
    const message=document.getElementById('message');
    await reaper.SetExtState('StartupProbe',guid,JSON.stringify({token,native,
      startup:d.startup,presentationMs:d.jsfx.presentationMs,controllerPrewarmed:d.controllerPrewarmed,idleWebViews:d.idleWebViews,
      measures:performance.getEntriesByType('measure').filter(x=>x.name.startsWith('reaweb.jsfx.')).map(x=>x.toJSON()),
      navigation:performance.getEntriesByType('navigation')[0]?.toJSON(),
      resources:performance.getEntriesByType('resource').slice(0,64).map(({initiatorType,startTime,duration})=>({initiatorType,startTime,duration})),
      error:message&&!message.hidden?message.textContent:''}),false);
  }catch(e){await reaper.SetExtState('StartupProbe','error',e.message,false);}
  finally{busy=false;}
},100);
''', encoding='utf-8')
(root / 'EmptyVST').mkdir()
(root / 'reaper.ini').write_text('[REAPER]\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nvst_scan=0\nerrnowarn=5\nhasrecentlyopened=1\nvstfullstate=49989\n'
    f'vstpath64={root / "EmptyVST"}\n[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n', encoding='utf-8')
if sys.platform != 'win32':
    config = root / 'reaper.ini'
    text = config.read_text().replace('[REAPER]', '[reaper]\naudiomode=4')
    if sys.platform == 'darwin': text = text.replace('[audioconfig]\nmode=4', '[audioconfig]\nmode=0')
    else: text = text.replace('audiomode=4', 'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2')
    config.write_text(text, encoding='utf-8')
for name in ('reaper-install-rev.txt','reaper-license.rk'):
    if (args.reaper.parent / name).is_file(): shutil.copy2(args.reaper.parent / name, root / name)
script = r'''
local root=ROOT
local function write(name,text) local f=assert(io.open(root..'/'..name,'a'));f:write(text..'\n');f:close() end
local function quit()
  reaper.Main_SaveProjectEx(0,root..'/acceptance.rpp',8)
  reaper.Main_SaveProject(0,false)
  reaper.Main_OnCommand(40004,0)
end
local function main()
write('started.txt','started')
local tracks,cases={},{}
for i=0,19 do
  reaper.InsertTrackAtIndex(i,false)
  local track=reaper.GetTrack(0,i);tracks[#tracks+1]=track
  reaper.SetMediaTrackInfo_Value(track,'B_MAINSEND',0)
  assert(reaper.TrackFX_AddByName(track,'startup-1175/1175 Compressor.jsfx',false,-1)==0)
  cases[#cases+1]={track=track,fx=0,kind='track-'..i}
end
for fx=1,2 do
  assert(reaper.TrackFX_AddByName(tracks[1],'startup-1175/1175 Compressor.jsfx',false,-1)==fx)
  cases[#cases+1]={track=tracks[1],fx=fx,kind='same-track-'..fx}
end
for i=0,2 do cases[#cases+1]={track=tracks[1],fx=i,kind='cached-'..i} end
for i=0,2 do cases[#cases+1]={track=tracks[1],fx=i,kind='simultaneous-'..i,keep=true} end
local index,deadline,previous,token=0,0,nil,''
local function advance()
  if previous and not previous.keep then reaper.TrackFX_Show(previous.track,previous.fx,2) end
  index=index+1
  local c=cases[index]
  if not c then write('done.txt','PASS');quit();return end
  previous=c;token=tostring(index)
  reaper.SetExtState('StartupProbe','token',token,false)
  reaper.TrackFX_SetParamNormalized(c.track,c.fx,0,index/40)
  reaper.TrackFX_Show(c.track,c.fx,3)
  deadline=reaper.time_precise()+25
end
advance()
local function tick()
  local ok,err=pcall(function()
    local c=cases[index];if not c then return end
    local error=reaper.GetExtState('StartupProbe','error');assert(error=='',error)
    assert(reaper.time_precise()<deadline,'Timed out: '..c.kind)
    local guid=reaper.TrackFX_GetFXGUID(c.track,c.fx)
    local packet=reaper.GetExtState('StartupProbe',guid)
    local value=tonumber(packet:match('"native":([%d%.]+)'))
    if packet:find('"token":"'..token..'"',1,true) and value and math.abs(value-index/40)<0.002 then
      write('samples.jsonl','{"case":"'..c.kind..'","expected":'..(index/40)..',"result":'..packet..'}')
      advance()
    end
  end)
  if not ok then write('error.txt',tostring(err));quit();return end
  if cases[index] then reaper.defer(tick) end
end
reaper.defer(tick)
end
local ok,err=xpcall(main,debug.traceback)
if not ok then write('error.txt',err);quit() end
'''
(root / 'launch.lua').write_text(script.replace('ROOT', '[[' + root.as_posix() + ']]'), encoding='utf-8')
with (root / 'process.log').open('w') as log:
    process = subprocess.Popen([str(args.reaper), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')], stdout=log, stderr=log)
    try: process.wait(timeout=240)
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
assert not (root / 'error.txt').exists(), (root / 'error.txt').read_text()
assert (root / 'done.txt').is_file(), 'REAPER did not complete the startup probe'
rows = [json.loads(line) for line in (root / 'samples.jsonl').read_text().splitlines()]
assert len(rows) == 28, len(rows)
for row in rows:
    assert abs(row['expected'] - row['result']['native']) < 0.002, row
    assert not row['result']['error'], row
    assert row['result']['startup']['sessionId'], row
print(json.dumps({'passed':True,'samples':len(rows),'output':str(root)}))
