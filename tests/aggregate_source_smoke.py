"""Verify aggregate-source PCM and stream lifecycle in an isolated real REAPER."""
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
for name, amplitude in [('a', .75), ('b', .5), ('c', .25), ('d', -.25), ('own', .125)]:
    with wave.open(str(root / f'{name}.wav'), 'wb') as wav:
        wav.setparams((2, 2, 48000, 0, 'NONE', ''))
        wav.writeframes(b''.join(struct.pack('<hh', *([round(32768 * amplitude * math.sin(2 * math.pi * i / 64))] * 2)) for i in range(48000 * 3)))
(root / 'index.html').write_text('<!doctype html><title>Aggregate source verification</title><script src="app.js"></script>', encoding='utf-8')
(root / 'launch.lua').write_text(r'''
local root=debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])')
local ok,err=xpcall(function()
  local tracks={}
  for i,name in ipairs({'Root','A','Nested','B','Bus','C','D','Other'}) do
    reaper.InsertTrackAtIndex(i-1,false)
    tracks[i]=reaper.GetTrack(0,i-1)
    reaper.GetSetMediaTrackInfo_String(tracks[i],'P_NAME',name,true)
  end
  for i,name in ipairs({'own','a','own','b','own','c','d','b'}) do
    local item=reaper.AddMediaItemToTrack(tracks[i])
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',3)
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0)
    reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    local take=reaper.AddTakeToMediaItem(item)
    reaper.SetMediaItemTake_Source(take,reaper.PCM_Source_CreateFromFile(root..name..'.wav'))
  end
  reaper.SetMediaTrackInfo_Value(tracks[1],'I_FOLDERDEPTH',1)
  reaper.SetMediaTrackInfo_Value(tracks[3],'I_FOLDERDEPTH',1)
  reaper.SetMediaTrackInfo_Value(tracks[4],'I_FOLDERDEPTH',-2)
  reaper.CreateTrackSend(tracks[6],tracks[5])
  reaper.SetOnlyTrackSelected(tracks[2])
  reaper.SetEditCurPos(.5,false,false)
  reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0),'D_VOL',0)
  reaper.UpdateArrange()
  local id=reaper.ReaWeb_Open(root..'index.html')
  assert(id>0,reaper.ReaWeb_GetLastError())
end,debug.traceback)
if not ok then local f=io.open(root..'launcher-error.txt','w');f:write(err);f:close() end
''', encoding='utf-8')
(root / 'app.js').write_text(r'''
(async()=>{
 const checks=[],metrics={},sleep=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(v,name)=>{if(!v)throw Error(name);checks.push(name);};
 const near=(a,b)=>Math.abs(a-b)<.003;
 const until=async(fn)=>{for(let i=0;i<200;i++){if(fn())return;await sleep(20);}throw Error('Timeout waiting for stream');};
 const peak=data=>Math.max(...Array.from(data,Math.abs));
 let active;
 try {
  await reaper.lifecycle.ready;
  metrics.extensionVersion=(await reaper.debug.getDiagnostics()).version;
  check(metrics.extensionVersion==='0.3.8.1','extension version');
  const t=[]; for(let i=0;i<8;i++)t.push(await reaper.GetTrack(0,i));
  const source=async i=>'track:'+await reaper.GetTrackGUID(t[i]);
  const opts=async i=>({source:await source(i),aggregate:true,fftSize:2048,updateRate:30});
  metrics.switchMs=[];
  for(let i=0;i<20;i++){
   const options=await opts(i%2?4:0),started=performance.now();
   const streams=await Promise.all(['meter','spectrum','waveform'].map(kind=>reaper.audio.openStream(kind,options)));
   try{
    await until(()=>streams.every(stream=>stream.latest()));
    metrics.switchMs.push(Math.round(performance.now()-started));
    check(streams.every(stream=>!stream.closed),'parallel analysis switch '+i);
   }finally{await Promise.all(streams.map(stream=>stream.close()));}
  }
  await sleep(100);
  check((await reaper.stream.getDiagnostics()).streams.length===0,'rapid switches release all producers');
  const take=async(kind,options)=>{
   const before=await reaper.GetProjectStateChangeCount(0),undo=await reaper.Undo_CanUndo2(0);
   const stream=await reaper.audio.openStream(kind,options); active=stream;
   await until(()=>stream.latest()); await sleep(180);
   const packet=stream.latest(),data=Array.from(packet.data);
   check(stream.info.channels===2,kind+' stereo');
   await stream.close();active=null;await sleep(60);
   check(await reaper.GetProjectStateChangeCount(0)===before&&await reaper.Undo_CanUndo2(0)===undo,'stream preserves project and Undo');
   return data;
  };
  for(const kind of ['audio','spectrum','meter','waveform']){
   const plain=await take(kind,{source:'selected-track',fftSize:2048});
   const disabled=await take(kind,{source:'selected-track',aggregate:false,fftSize:2048});
   const aggregate=await take(kind,{source:'selected-track',aggregate:true,fftSize:2048});
   const count=kind==='meter'?4:plain.length;
   check(plain.length===aggregate.length&&plain.slice(0,count).every((v,i)=>near(v,aggregate[i])&&near(v,disabled[i])),kind+' single-track/default/false equivalence');
   const folder=await take(kind,await opts(0));
   // Compare spectral gain at the actual device rate, which can place the tone between FFT bins.
   const expected=kind==='spectrum'?peak(plain)*2:1.5;
   check(near(kind==='meter'?folder[0]:peak(folder),expected),kind+' nested folder sum above unity');
  }
  const pcm=async(i,expected,name)=>{const data=await take('audio',await opts(i));check(near(peak(data),expected),name);};
  await pcm(4,.375,'receive bus includes own source and upstream');
  await reaper.SetMediaTrackInfo_Value(t[1],'B_MUTE',1);await pcm(0,.75,'track mute');await reaper.SetMediaTrackInfo_Value(t[1],'B_MUTE',0);
  await reaper.SetMediaTrackInfo_Value(t[2],'B_MUTE',1);await pcm(0,.875,'nested parent mute');await reaper.SetMediaTrackInfo_Value(t[2],'B_MUTE',0);
  await reaper.SetMediaTrackInfo_Value(t[0],'B_MUTE',1);await pcm(0,0,'root mute');await reaper.SetMediaTrackInfo_Value(t[0],'B_MUTE',0);
  await reaper.SetMediaTrackInfo_Value(t[1],'I_SOLO',2);await pcm(0,.75,'solo child excludes other source branches');await reaper.SetMediaTrackInfo_Value(t[1],'I_SOLO',0);
  await reaper.SetMediaTrackInfo_Value(t[2],'I_SOLO',2);await pcm(0,.625,'solo nested folder');await reaper.SetMediaTrackInfo_Value(t[2],'I_SOLO',0);
  await reaper.SetMediaTrackInfo_Value(t[7],'I_SOLO',2);await pcm(0,0,'unrelated solo excludes root');
  await reaper.SetMediaTrackInfo_Value(t[1],'B_SOLO_DEFEAT',1);await pcm(0,.75,'solo defeat');await reaper.SetMediaTrackInfo_Value(t[1],'B_SOLO_DEFEAT',0);await reaper.SetMediaTrackInfo_Value(t[7],'I_SOLO',0);
  const item=await reaper.GetTrackMediaItem(t[1],0);
  await reaper.SetMediaItemInfo_Value(item,'B_MUTE',1);await pcm(0,.75,'muted item follows accessor PCM');await reaper.SetMediaItemInfo_Value(item,'B_MUTE',0);
  await reaper.SetMediaTrackInfo_Value(t[1],'B_MAINSEND',0);await pcm(0,.75,'disabled parent send');await reaper.SetMediaTrackInfo_Value(t[1],'B_MAINSEND',1);
  await reaper.SetTrackSendInfo_Value(t[5],0,0,'B_MUTE',1);await pcm(4,.125,'muted receive');await reaper.SetTrackSendInfo_Value(t[5],0,0,'B_MUTE',0);
  const busItem=await reaper.GetTrackMediaItem(t[4],0);await reaper.SetMediaItemInfo_Value(busItem,'B_MUTE',1);
  await reaper.CreateTrackSend(t[6],t[4]);await pcm(4,0,'opposite-phase source PCM cancels');
  for(const kind of ['spectrum','meter','waveform']){
   const data=await take(kind,await opts(4));
   check(peak(kind==='meter'?data.slice(0,4):data)<.0001,kind+' analyzes PCM cancellation');
  }
  const dup=await reaper.CreateTrackSend(t[1],t[0]);await pcm(0,1.5,'duplicate folder and receive path counted once');
  const cycle=await reaper.CreateTrackSend(t[0],t[1]);await pcm(0,1.5,'routing cycle terminates and deduplicates');
  await reaper.RemoveTrackSend(t[0],0,cycle);await reaper.RemoveTrackSend(t[1],0,dup);
  active=await reaper.audio.openStream('audio',await opts(0));await until(()=>active.latest());
  check((await reaper.debug.getDiagnostics()).streams.streams.length===1,'one public native stream');
  await reaper.SetMediaTrackInfo_Value(t[1],'B_MUTE',1);await sleep(250);check(near(peak(active.latest().data),.75),'live mute refresh');
  await reaper.SetMediaTrackInfo_Value(t[1],'B_MUTE',0);await sleep(250);check(near(peak(active.latest().data),1.5),'live unmute refresh');
  const sequence=active.latest().sequence;await reaper.SetOnlyTrackSelected(t[7]);await sleep(100);
  check(active.latest().sequence>sequence&&near(peak(active.latest().data),1.5),'root remains bound after selection change');
  const route=await reaper.CreateTrackSend(t[6],t[0]);await sleep(250);check(near(peak(active.latest().data),1.25),'live receive added');
  await reaper.RemoveTrackSend(t[6],0,route);await sleep(250);check(near(peak(active.latest().data),1.5),'live receive removed');
  await reaper.CreateTrackSend(t[6],t[0]);await sleep(150);await reaper.DeleteTrack(t[6]);
  await sleep(250);check(!active.closed&&near(peak(active.latest().data),1.5),'upstream deletion preserves root stream');
  await reaper.OnPlayButton();
  await until(()=>{while(active.read()){}const packet=active.latest();metrics.playback={timestamp:packet.timestamp,peak:peak(packet.data)};return packet.timestamp>.5&&near(metrics.playback.peak,1.5);});
  check((await reaper.GetPlayState()&1)!==0,'folder aggregate follows playback time');
  await reaper.OnStopButton();
  await active.close(); active=null;await sleep(100);
  check((await reaper.debug.getDiagnostics()).streams.streams.length===0,'last consumer releases native stream');
  for(const source of ['master','input']){
   let code;try{await reaper.audio.openStream('audio',{source,aggregate:true});}catch(e){code=e.code;}
   check(code==='INVALID_ARGUMENT',source+' aggregate rejected');
  }
  active=await reaper.audio.openStream('audio',await opts(4));await until(()=>active.latest());
  await reaper.DeleteTrack(t[4]);await until(()=>active.closed);check(active.closed,'root deletion closes stream');active=null;
  metrics.reaperVersion=await reaper.GetAppVersion();
  await reaper.fs.writeText('result.json',JSON.stringify({passed:true,checks,metrics}));
 }catch(error){
  if(active)await active.close().catch(()=>{});
  await reaper.fs.writeText('result.json',JSON.stringify({passed:false,checks,metrics,error:String(error),stack:error.stack}));
 }
})();
''', encoding='utf-8')
command = [str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')]
with (root / 'process.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen(command, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 180
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
