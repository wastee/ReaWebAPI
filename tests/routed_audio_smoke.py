"""Exercise routed PCM and analysis in an isolated REAPER resource directory."""
import argparse
import json
import math
import secrets
import socket
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import wave
from external_client_contract import WebSocket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reaper', type=Path, required=True)
parser.add_argument('--extension', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / 'UserPlugins').mkdir()
shutil.copy2(args.extension, root / 'UserPlugins' / args.extension.name)
if sys.platform.startswith('linux'):
    for helper in args.extension.parent.glob('reawebapi-webview-*'):
        shutil.copy2(helper, root / 'UserPlugins' / helper.name)
config = '[REAPER]\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nerrnowarn=5\nhasrecentlyopened=1\naudioclosestop=0\naudiocloseinactive=0\n'
(root / 'EmptyVST').mkdir()
config += f'vst_scan=0\nvstpath64={root / "EmptyVST"}\nvstfullstate=49989\n'
if sys.platform.startswith('linux'):
    config += 'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n'
else:
    config += '[audioconfig]\nmode=4\ndummy_srate=48000\ndummy_blocksize=512\n'
(root / 'reaper.ini').write_text(config)
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
token = secrets.token_hex(32)
(root / 'ReaWebAPI.ini').write_text(f'[ExternalClients]\nEnabled=1\nPort={port}\nAccessToken={token}\n')
with wave.open(str(root / 'tone.wav'), 'wb') as wav:
    wav.setparams((2, 2, 48000, 0, 'NONE', ''))
    wav.writeframes(b''.join(struct.pack('<hh', int(8192 * math.sin(2 * math.pi * 440 * i / 48000)),
        int(8192 * math.sin(2 * math.pi * 880 * i / 48000))) for i in range(48000 * 30)))
(root / 'Effects/Test').mkdir(parents=True)
(root / 'Effects/Test/gain').write_text('desc:Routed Audio Test Gain\n@sample\nspl0*=0.5; spl1*=0.5;\n')
(root / 'Effects/Test/map').write_text('desc:Routed Audio Test Map\n@sample\nspl0=spl2; spl1=spl3;\n')
(root / 'index.html').write_text('<!doctype html><title>Routed audio verification</title><script src="app.js"></script>')
(root / 'app.js').write_text(r'''
(async()=>{
 const checks=[],metrics={},sleep=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(ok,name)=>{if(!ok)throw Error(name);checks.push(name);};
 const until=async(fn,name)=>{for(let i=0;i<200;i++){if(await fn()){checks.push(name);return;}await sleep(30);}throw Error('Timeout: '+name);};
 const open=async(kind,track,tap='post-fx')=>reaper.audio.openStream(kind,{source:'track:'+await reaper.GetTrackGUID(track),tap,updateRate:30});
 const level=async(s,value,name)=>{await sleep(400);await until(()=>s.latest()&&Math.abs(s.latest().data[0]-value)<0.012,name);};
 try {
  await reaper.lifecycle.ready;
  const parent=await reaper.GetTrack(0,0),nested=await reaper.GetTrack(0,1),source=await reaper.GetTrack(0,2),bus=await reaper.GetTrack(0,3);
  const parentMeter=await open('meter',parent),spectrum=await open('spectrum',parent),waveform=await open('waveform',parent);
  check(await reaper.TrackFX_GetCount(parent)===1,'analysis consumers share one tap');
  check(parentMeter.info.source.endsWith(':post-fx'),'tap identity is explicit');
  await reaper.OnPlayButton();
  await level(parentMeter,0.125,'nested folders carry child post-FX signal');
  await until(()=>spectrum.latest()?.data.some(x=>x>0.05),'folder spectrum has real samples');
  await until(()=>waveform.latest()?.data.some(x=>x>0.05),'folder waveform has real samples');
  const pcm=await open('audio',bus),meter=await open('meter',bus);
  await level(meter,0.0625,'post-fader receive includes send gain');
  await until(()=>pcm.latest()?.data.some(x=>Math.abs(x)>0.04),'receive PCM contains audio');
  await reaper.SetMediaTrackInfo_Value(source,'D_VOL',0.5);
  await level(meter,0.03125,'source fader affects post-fader receive');
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_SENDMODE',3);
  await level(meter,0.0625,'pre-fader post-FX receive');
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_SENDMODE',1);
  await level(meter,0.125,'pre-FX receive');
  await reaper.SetTrackSendInfo_Value(source,0,0,'B_MUTE',1);
  await level(meter,0,'muted send is silent');
  await reaper.SetTrackSendInfo_Value(source,0,0,'B_MUTE',0);
  await level(meter,0.125,'unmuted receive resumes');
  await reaper.TrackFX_AddByName(bus,'JS: Test/gain',false,-1);
  await level(meter,0.0625,'capture follows effects appended during analysis');
  await reaper.SetMediaTrackInfo_Value(bus,'D_VOL',0.2);
  await level(meter,0.0625,'destination fader does not change pre-fader capture');
  await reaper.TrackFX_Delete(bus,0);
  await level(meter,0.125,'capture survives effect deletion');
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_SRCCHAN',1025);
  await level(meter,0.125,'mono right-channel send reaches receive');
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_SRCCHAN',0);
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_DSTCHAN',2);
  await reaper.SetMediaTrackInfo_Value(bus,'I_NCHAN',4);
  await level(meter,0,'channels 3-4 do not leak into stereo capture');
  await reaper.TrackFX_AddByName(bus,'JS: Test/map',false,-1);
  await level(meter,0.125,'multichannel receive mapped to channels 1-2');
  await reaper.TrackFX_Delete(bus,0);
  await reaper.SetTrackSendInfo_Value(source,0,0,'I_DSTCHAN',0);
  await reaper.SetMediaTrackInfo_Value(nested,'B_MAINSEND',0);
  await level(parentMeter,0,'disabled parent send is silent');
  await reaper.SetMediaTrackInfo_Value(nested,'B_MAINSEND',1);
  await level(parentMeter,0.0625,'parent send resumes with source fader');
  const legacy=await open('meter',parent,'pre-fx');
  await level(legacy,0,'legacy accessor excludes folder audio');await legacy.close();
  await spectrum.close();await waveform.close();
  await sleep(100);check(await reaper.TrackFX_GetCount(parent)===1,'one consumer keeps tap alive');
  await parentMeter.close();await until(async()=>await reaper.TrackFX_GetCount(parent)===0,'last consumer removes tap');
  await pcm.close();await meter.close();await until(async()=>await reaper.TrackFX_GetCount(bus)===0,'receive tap removed');
  await reaper.TrackFX_AddByName(bus,'JS: ReaWebAPI/track_audio_v1.jsfx',false,-1);
  const restored=await open('meter',bus);
  check(await reaper.TrackFX_GetCount(bus)===1,'inactive saved capture is replaced');
  await level(restored,0.125,'reopened capture reads fresh audio');
  await reaper.SetMediaTrackInfo_Value(bus,'I_FXEN',0);
  await until(()=>restored.closed,'FX chain bypass closes unavailable capture');
  let unavailable=false;try{await open('meter',bus);}catch(e){unavailable=e.code==='AUDIO_UNAVAILABLE';}
  check(unavailable,'opening bypassed chain reports unavailable');
  await reaper.SetMediaTrackInfo_Value(bus,'I_FXEN',1);
  await reaper.OnStopButton();
  const stopped=await open('meter',parent);await level(stopped,0,'stopped playback does not read cursor media');await stopped.close();await sleep(100);
  const deleted=await open('meter',bus);await reaper.DeleteTrack(bus);await until(()=>deleted.closed,'track deletion closes capture');
  await until(async()=>(await reaper.stream.getDiagnostics()).streams.length===0,'all producer resources released');
  await reaper.fs.writeText('result.json',JSON.stringify({passed:true,checks,metrics}));
 }catch(error){metrics.diagnostics=await reaper.debug.getDiagnostics();await reaper.fs.writeText('result.json',JSON.stringify({passed:false,checks,metrics,error:String(error),stack:error.stack}));}
})();
''', encoding='utf-8')
(root / 'launch.lua').write_text(r'''
local root=debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])')
local ok,err=xpcall(function()
for i=0,3 do reaper.InsertTrackAtIndex(i,false) end
local parent,nested,source,bus=reaper.GetTrack(0,0),reaper.GetTrack(0,1),reaper.GetTrack(0,2),reaper.GetTrack(0,3)
reaper.SetMediaTrackInfo_Value(parent,'I_FOLDERDEPTH',1)
reaper.SetMediaTrackInfo_Value(nested,'I_FOLDERDEPTH',1)
reaper.SetMediaTrackInfo_Value(source,'I_FOLDERDEPTH',-2)
reaper.SetOnlyTrackSelected(source)
reaper.InsertMedia(root..'tone.wav',0)
assert(reaper.TrackFX_AddByName(source,'JS: Test/gain',false,-1)>=0)
local send=reaper.CreateTrackSend(source,bus)
reaper.SetTrackSendInfo_Value(source,0,send,'D_VOL',0.5)
reaper.SetEditCurPos(0,false,false)
assert(reaper.ReaWeb_Open(root..'index.html')>0,reaper.ReaWeb_GetLastError())
end,debug.traceback)
if not ok then local f=io.open(root..'launcher-error.txt','w');f:write(err);f:close() end
''')
with (root / 'process.log').open('w') as log:
    startup = None
    if sys.platform == 'win32':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    process = subprocess.Popen([str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-nosplash', '-splashlog', str(root / 'startup.log')], stdout=log, stderr=log, startupinfo=startup)
    try:
        deadline = time.monotonic() + 100
        client = None
        while process.poll() is None and time.monotonic() < deadline:
            try:
                client = WebSocket(f'ws://127.0.0.1:{port}')
                break
            except OSError:
                time.sleep(.2)
        if client:
            try:
                client.call('auth.authenticate', {'token': token, 'protocolVersion': 1})
                action = client.call('api.call', {'name': 'AddRemoveReaScript', 'args': [True, 0, str(root / 'launch.lua'), True]})
                client.call('api.call', {'name': 'Main_OnCommand', 'args': [action, 0]})
            finally:
                client.close()
        while not (root / 'result.json').exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.2)
        result = json.loads((root / 'result.json').read_text()) if (root / 'result.json').exists() else {'passed': False, 'error': 'No report'}
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
