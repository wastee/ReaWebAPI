"""Stress nested folder playback, analysis reconnects and bridge progress in real REAPER."""
import argparse
import configparser
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reaper', type=Path, required=True)
parser.add_argument('--extension', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--audio-config', type=Path)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / 'UserPlugins').mkdir()
config = '[REAPER]\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\naudioclosestop=0\naudiocloseinactive=0\n'
if sys.platform == 'darwin':
    config = config.replace('[REAPER]', '[reaper]')
    if args.audio_config:
        audio = configparser.ConfigParser(strict=False, interpolation=None)
        audio.read(args.audio_config, encoding='utf-8-sig')
        section = next(name for name in audio.sections() if name.lower() == 'reaper')
        config += ''.join(f'{key}={value}\n' for key, value in audio[section].items() if key.startswith('coreaudio'))
    config += 'hasrecentlyopened=1\n[audioconfig]\nmode=0\n'
elif sys.platform == 'win32':
    effects = args.reaper.resolve().parent / 'Plugins' / 'FX'
    config += f'vst_scan=0\nvstpath={effects}\nvstpath64={effects}\nvstfullstate=49989\n'
    config += '[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_in=0\nwaveout_nch_out=2\n'
else:
    config += 'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n'
(root / 'reaper.ini').write_text(config, encoding='utf-8')
target = root / 'UserPlugins' / args.extension.name
shutil.copy2(args.extension, target)
if sys.platform == 'darwin':
    subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
if sys.platform.startswith('linux'):
    for helper in args.extension.parent.glob('reawebapi-webview-*'):
        shutil.copy2(helper, root / 'UserPlugins' / helper.name)
for name, amplitude in [('tone', .01)]:
    with wave.open(str(root / f'{name}.wav'), 'wb') as wav:
        wav.setparams((2, 2, 48000, 0, 'NONE', ''))
        wav.writeframes(b''.join(struct.pack('<hh', *([round(32768 * amplitude * math.sin(2 * math.pi * i / 64))] * 2)) for i in range(48000 * 3)))
(root / 'index.html').write_text('<!doctype html><title>Aggregate source verification</title><script src="app.js"></script>', encoding='utf-8')
(root / 'launch.lua').write_text(r'''
local root=debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])')
local tracks={}
for i=0,80 do
 reaper.InsertTrackAtIndex(i,false)
 tracks[i+1]=reaper.GetTrack(0,i)
end
for i=0,80 do
 local t=tracks[i+1]
 local offset=(i-1)%5
 local depth=i==0 and 1 or (offset<2 and 1 or (offset==4 and -2 or 0))
 if i==80 then depth=-3 end
 reaper.SetMediaTrackInfo_Value(t,'I_FOLDERDEPTH',depth)
 reaper.GetSetMediaTrackInfo_String(t,'P_NAME','Stress '..i,true)
 if i>0 and offset>=2 then
  local item=reaper.AddMediaItemToTrack(t);reaper.SetMediaItemInfo_Value(item,'D_LENGTH',3)
  reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
  reaper.SetMediaItemTake_Source(reaper.AddTakeToMediaItem(item),reaper.PCM_Source_CreateFromFile(root..'tone.wav'))
 end
end
reaper.SetOnlyTrackSelected(tracks[1]);reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0),'D_VOL',0)
reaper.UpdateArrange()
assert(reaper.ReaWeb_Open(root..'index.html')>0,reaper.ReaWeb_GetLastError())
''',encoding='utf-8')
(root / 'app.js').write_text(r'''
(async()=>{
 const sleep=ms=>new Promise(r=>setTimeout(r,ms)),metrics={switchMs:[]},checks=[];let streams=[];
 const check=(ok,name)=>{if(!ok)throw Error(name);checks.push(name);};
 const until=async fn=>{const end=performance.now()+10000;while(performance.now()<end){if(fn())return;await sleep(20);}throw Error('No native audio data within 10 seconds');};
 try{
  await reaper.lifecycle.ready;
  metrics.version=(await reaper.debug.getDiagnostics()).version;
  check(metrics.version==='0.3.8.2','extension version');
  const root=await reaper.GetTrack(0,0),child=await reaper.GetTrack(0,1);
  await reaper.GetSet_LoopTimeRange(true,true,0,3,false);await reaper.GetSetRepeat(1);
  await reaper.SetEditCurPos(0,false,false);await reaper.OnPlayButton();
  check((await reaper.GetPlayState()&1)!==0,'playback started');
  for(let i=0;i<60;i++){
   await reaper.SetOnlyTrackSelected(i%2?child:root);
   const start=performance.now();
   streams=await Promise.all(['meter','spectrum','waveform'].map(kind=>reaper.audio.openStream(kind,{source:'selected-track',aggregate:true})));
   await until(()=>streams.every(s=>s.closed||s.latest()));
   check(streams.every(s=>!s.closed&&s.latest()),'three analysis streams connected '+i);
   metrics.lastMeter=Array.from(streams[0].latest().data);metrics.source=streams[0].info.source;metrics.playState=await reaper.GetPlayState();metrics.position=await reaper.GetPlayPosition();
   await until(()=>streams[0].closed||streams[0].latest().data[0]>(i%2?.025:.4));
   check(!streams[0].closed,'selected folder includes child PCM '+i);
   metrics.switchMs.push(Math.round(performance.now()-start));
   if(i===0){
    let previous=streams.map(s=>s.latest().sequence),lastPosition=0,loops=0;
    for(let n=0;n<60;n++){
     await sleep(1000);
     check(streams.every((s,j)=>!s.closed&&s.latest()?.sequence>previous[j]),'continuous parent delivery '+n);
     previous=streams.map(s=>s.latest().sequence);
     const position=await reaper.GetPlayPosition();if(position<lastPosition)loops++;lastPosition=position;
    }
    metrics.loops=loops;check(loops>=3,'repeated playback crosses loop boundaries');
   }
   await Promise.all(streams.map(s=>s.close()));streams=[];
  }
  await sleep(100);
  metrics.diagnostics=await reaper.stream.getDiagnostics();
  check(metrics.diagnostics.streams.length===0&&metrics.diagnostics.consumers===0,'all producers and consumers released');
  check(metrics.diagnostics.transport.handshakeFailures===0&&metrics.diagnostics.transport.expiredTickets===0,'no handshake failures or expired tickets');
  await reaper.fs.writeText('result.json',JSON.stringify({passed:true,checks,metrics}));
 }catch(error){
  metrics.failures=streams.map(s=>({name:s.info.name,closed:s.closed,error:s.error?.code,message:s.error?.message,sequence:String(s.latest()?.sequence)}));
  await Promise.allSettled(streams.map(s=>s.close()));
  metrics.diagnostics=await reaper.debug.getDiagnostics();
  await reaper.fs.writeText('result.json',JSON.stringify({passed:false,error:String(error),checks,metrics}));
 }
})();
''',encoding='utf-8')
command = [str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')]
with (root / 'process.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen(command, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 240
        while not (root / 'result.json').exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.2)
        result = json.loads((root / 'result.json').read_text(encoding='utf-8')) if (root / 'result.json').exists() else {'passed': False, 'error': 'No report'}
        print(json.dumps({'resource': str(root), **result}, ensure_ascii=False), flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
raise SystemExit(0 if result['passed'] else 1)
