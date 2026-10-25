"""Verify JSFX metadata and value updates in a new disposable REAPER profile."""
import argparse
import configparser
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
for name in ('reaper', 'extension', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--chain', action='store_true', help='Use FX chain editors instead of floating editors')
args = parser.parse_args()
source = Path(__file__).resolve().parents[1]
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / 'ReaWebAPI.ini').write_text('[ReaWebAPI]\nIdleWebViews=0\n')
(root / 'UserPlugins').mkdir()
binary = root / 'UserPlugins' / args.extension.name
shutil.copy2(args.extension, binary)
if sys.platform == 'darwin':
    subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
elif sys.platform.startswith('linux'):
    helper = args.extension.parent / ('reawebapi-webview-' + args.extension.stem.split('reaper_reawebapi-', 1)[1])
    shutil.copy2(helper, root / 'UserPlugins' / helper.name)
    (root / 'UserPlugins' / helper.name).chmod(0o755)
effect = root / 'Effects/metadata'
effect.mkdir(parents=True)
shutil.copy2(source / 'runtime/jsfx-webui.mjs', effect)
(effect / 'probe.jsfx').write_text('''desc:Metadata acceptance
slider1:0<0,2,1{Off,Low,High}>Mode
slider2:0<0,2,1>Numeric steps
slider3:440<20,20000,1>Frequency (Hz)
slider4:0<-60,12,0.1>Gain (dB)
slider5:10<0,1000,1>Time (ms)
slider6:50<0,100,1>Mix (%)
slider7:0.5<0,1,0.01>Filter / Amount
slider8:0<0,0,1{Only}>Single option
slider10:0<0,1,1{Off,On}>-Hidden switch
slider12:mode=0<0,2,1{Same,,Same}>Aliased choices
slider14:0<0,1,1{低,高}>Unicode choices
slider16:/enum-files:default.wav:File selector
@sample
spl0=0; spl1=0;
''', encoding='utf-8')
(root / 'Data/enum-files').mkdir(parents=True)
(root / 'Data/enum-files/default.wav').write_bytes(b'')
(root / 'Data/enum-files/other.wav').write_bytes(b'')
(effect / 'probe.json').write_text(json.dumps({'schema': 1, 'JSFX_WebUI': {'entry': 'index.html'}}))
(effect / 'index.html').write_text('''<!doctype html><meta charset="utf-8"><title>Metadata acceptance</title>
<pre id="status">Connecting</pre><script type="module">
import {connectJSFX} from './jsfx-webui.mjs';
const check=(value,message)=>{if(!value)throw Error(message);};
const report=async e=>{await reaper.SetExtState('Metadata','error',e.stack||e.message,false);};
try {
  const fx=await connectJSFX(reaper,report), initial=fx.params.snapshot;
  await reaper.SetExtState('Metadata','initial',JSON.stringify(initial),false);
  const metadata=p=>JSON.stringify([p.name,p.ident,p.min,p.max,p.defaultNormalized,p.enum]);
  const baseline=initial.parameters.map(metadata);
  const verify=state=>{
    check(state.parameters.length===15,'Parameter count: '+state.parameters.length);
    for(const p of state.parameters){
      check(!('unit' in p)&&!('section' in p)&&!('reaweb' in p),'Removed metadata fields');
      check(metadata(p)===baseline[p.index],'Metadata changed during value update');
    }
  };
  verify(initial);
  check(JSON.stringify(initial.parameters[0].enum)===JSON.stringify([
    {label:'Off',value:0},{label:'Low',value:1},{label:'High',value:2}]),'Loaded enum table: '+JSON.stringify(initial));
  const choices=index=>initial.parameters[index].enum;
  check(choices(7)?.length===1&&choices(7)[0].label==='Only','Single option');
  check(choices(8)?.length===2&&choices(8)[1].label==='On','Hidden sparse slider');
  check(choices(9)?.length===2&&choices(9)[0].label==='Same'&&choices(9)[1].label==='Same','Aliased choices: engine skips empty declarations');
  check(choices(10)?.length===2&&choices(10)[1].label==='高','Unicode labels');
  for(const p of initial.parameters)if(![0,7,8,9,10].includes(p.index))check(p.enum===null,'Unexpected enum: '+p.name);
  check(initial.parameters[0].formatted==='Off','Initial enum display');
  let changes=0;
  await fx.params.onChange(state=>{try {verify(state);changes++;}catch(e){report(e);}});
  await fx.params.beginEdit(0); await fx.params.set(0,1); await fx.params.endEdit();
  check(fx.params.snapshot.parameters[0].formatted==='High','Enum write');
  await reaper.SetExtState('Metadata','ready','1',false);
  const deadline=Date.now()+15000;
  while(fx.params.snapshot.parameters[0].formatted!=='Low'){
    check(Date.now()<deadline,'Host update timed out');await new Promise(r=>setTimeout(r,50));
  }
  verify(fx.params.snapshot);check(changes>0,'Missing onChange');
  const refreshed=await fx.params.getState();verify(refreshed);
  const result={initial,final:refreshed,changes};
  document.getElementById('status').textContent=JSON.stringify(result);
  await reaper.SetExtState('Metadata','result',JSON.stringify(result),false);
}catch(e){await report(e);}
</script>''', encoding='utf-8')
(root / 'EmptyVST').mkdir()
settings = ('[reaper]\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nerrnowarn=5\nhasrecentlyopened=1\n'
            'newprojdo=0\nvst_scan=0\nvstfullstate=49989\nvstpath64=' + str(root / 'EmptyVST') + '\n')
if sys.platform.startswith('linux'):
    settings += 'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n'
elif sys.platform == 'win32':
    settings += '[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n'
elif sys.platform == 'darwin':
    existing = configparser.ConfigParser(interpolation=None, strict=False)
    existing.read(Path.home() / 'Library/Application Support/REAPER/reaper.ini', encoding='utf-8')
    if existing.has_section('reaper'):
        settings += ''.join(key + '=' + value + '\n' for key, value in existing.items('reaper') if key.startswith('coreaudio'))
(root / 'reaper.ini').write_text(settings)
resource = Path.home() / 'Library/Application Support/REAPER' if sys.platform == 'darwin' else args.reaper.parent
for name in ('reaper-install-rev.txt', 'reaper-license.rk'):
    if (resource / name).is_file():
        shutil.copy2(resource / name, root / name)
lua = r'''
local root=ROOT
local chain=CHAIN
local function write(name,text) local f=assert(io.open(root..'/'..name,'w'));f:write(text);f:close() end
write('version.txt',reaper.GetAppVersion())
local function quit()
  reaper.Main_SaveProjectEx(0,root..'/acceptance.rpp',8)
  reaper.Main_SaveProject(0,false)
  reaper.Main_OnCommand(40004,0)
end
local function main()
  reaper.InsertTrackAtIndex(0,false)
  local track=reaper.GetTrack(0,0)
  reaper.SetMediaTrackInfo_Value(track,'B_MAINSEND',0)
  local item=reaper.AddMediaItemToTrack(track)
  local take=reaper.AddTakeToMediaItem(item)
  reaper.SetMediaItemInfo_Value(item,'D_LENGTH',1)
  local master=reaper.GetMasterTrack(0)
  local cases={{kind='track',owner=track,prefix='TrackFX_'},{kind='take',owner=take,prefix='TakeFX_'},
    {kind='input',owner=track,prefix='TrackFX_',input=true},{kind='master',owner=master,prefix='TrackFX_'},
    {kind='monitoring',owner=master,prefix='TrackFX_',input=true}}
  local sourcefile=root..'/Effects/metadata/probe.jsfx'
  local f=assert(io.open(sourcefile,'r'));local original=f:read('*a');f:close()
  local function source(text) local out=assert(io.open(sourcefile,'w'));out:write(text);out:close() end
  local index,deadline,changed,show_at=0,0,false,0
  local function advance()
    if cases[index] then reaper[cases[index].prefix..'Show'](cases[index].owner,cases[index].fx,chain and 0 or 2) end
    index=index+1
    local c=cases[index]
    if not c then write('done.txt','PASS');quit();return end
    for _,key in ipairs({'ready','result','error','initial'}) do reaper.DeleteExtState('Metadata',key,false) end
    source(original)
    local add=reaper[c.prefix..'AddByName']
    local fx=c.prefix=='TrackFX_' and add(c.owner,'metadata/probe.jsfx',c.input or false,-1) or add(c.owner,'metadata/probe.jsfx',-1)
    assert(fx>=0,'Could not load JSFX');if c.input then fx=fx|0x1000000 end;c.fx=fx
    show_at=reaper.time_precise()+0.25
    deadline=show_at+45;changed=false
  end
  advance()
  local function tick()
    local ok,err=xpcall(function()
      local c=cases[index];if not c then return end
      if show_at>0 then
        if reaper.time_precise()>=show_at then
          reaper[c.prefix..'Show'](c.owner,c.fx,chain and 1 or 3);show_at=0
        end
        return
      end
      local snapshot=reaper.GetExtState('Metadata','initial');if snapshot~='' then write(c.kind..'-initial.json',snapshot) end
      local error=reaper.GetExtState('Metadata','error');assert(error=='',error)
      assert(reaper.time_precise()<deadline,'Metadata timeout: '..c.kind)
      if not changed and reaper.GetExtState('Metadata','ready')=='1' then
        assert(reaper[c.prefix..'GetParam'](c.owner,c.fx,0)==2,'SDK write failed')
        source(original:gsub('Off,Low,High','DiskA,DiskB,DiskC'))
        reaper[c.prefix..'SetParam'](c.owner,c.fx,0,1);changed=true
      end
      local result=reaper.GetExtState('Metadata','result')
      if result~='' then write(c.kind..'.json',result);advance() end
    end,debug.traceback)
    if not ok then write('error.txt',err);quit();return end
    if cases[index] then reaper.defer(tick) end
  end
  reaper.defer(tick)
end
local ready=reaper.time_precise()+1
local function start()
  if reaper.time_precise()<ready then reaper.defer(start);return end
  local ok,err=xpcall(main,debug.traceback)
  if not ok then write('error.txt',err);quit() end
end
reaper.defer(start)
'''
(root / 'launch.lua').write_text(lua.replace('ROOT', '[[' + root.as_posix() + ']]').replace('CHAIN', str(args.chain).lower()), encoding='utf-8')
with (root / 'process.log').open('w') as log:
    options = {'creationflags': subprocess.CREATE_NO_WINDOW} if sys.platform == 'win32' else {}
    process = subprocess.Popen([str(args.reaper), '-newinst', '-cfgfile', str(root / 'reaper.ini'),
                                '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')],
                               stdout=log, stderr=log, **options)
    try:
        if sys.platform == 'darwin':
            subprocess.run(['osascript', '-e', 'tell application "System Events" to set frontmost of '
                            f'first application process whose unix id is {process.pid} to true'], check=True, timeout=10)
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline and process.poll() is None:
            if (root / 'done.txt').exists() or (root / 'error.txt').exists():
                break
            time.sleep(.2)
    finally:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
assert not (root / 'error.txt').exists(), (root / 'error.txt').read_text()
assert (root / 'done.txt').exists(), 'REAPER did not complete metadata acceptance'
for kind in ('track', 'take', 'input', 'master', 'monitoring'):
    result = json.loads((root / (kind + '.json')).read_text(encoding='utf-8'))
    assert result['initial']['parameters'][0]['formatted'] == 'Off'
    assert result['final']['parameters'][0]['formatted'] == 'Low'
print(json.dumps({'passed': True, 'version': (root / 'version.txt').read_text(), 'output': str(root)}))
