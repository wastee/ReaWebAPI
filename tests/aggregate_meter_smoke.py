"""Verify aggregate RMS/LUFS history with empty source accessors in an isolated real REAPER."""
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
for name, amplitude in [('signal', .5), ('own', .25)]:
    with wave.open(str(root / f'{name}.wav'), 'wb') as wav:
        wav.setparams((2, 2, 48000, 0, 'NONE', ''))
        second = b''.join(struct.pack('<hh', *([round(32768 * amplitude * math.sin(2 * math.pi * i / 48))] * 2)) for i in range(48000))
        wav.writeframes(second * 120)
(root / 'index.html').write_text('<!doctype html><title>Aggregate meter acceptance</title><script src="app.js"></script>', encoding='utf-8')
(root / 'app.json').write_text(json.dumps({'id': 'reawebapi-aggregate-meter-test', 'name': 'Aggregate meter acceptance', 'entry': 'index.html'}), encoding='utf-8')
(root / 'launch.lua').write_text(r'''
local root=debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])')
local ok,err=xpcall(function()
  local tracks={}
  for i,name in ipairs({'Empty bus','Empty folder','Signal','Empty upstream','Own media'}) do
    reaper.InsertTrackAtIndex(i-1,false)
    tracks[i]=reaper.GetTrack(0,i-1)
    reaper.GetSetMediaTrackInfo_String(tracks[i],'P_NAME',name,true)
  end
  for _,entry in ipairs({{3,'signal'},{5,'own'}}) do
    local item=reaper.AddMediaItemToTrack(tracks[entry[1]])
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',120)
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0)
    reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    local take=reaper.AddTakeToMediaItem(item)
    reaper.SetMediaItemTake_Source(take,reaper.PCM_Source_CreateFromFile(root..entry[2]..'.wav'))
  end
  reaper.SetMediaTrackInfo_Value(tracks[2],'I_FOLDERDEPTH',1)
  reaper.SetMediaTrackInfo_Value(tracks[4],'I_FOLDERDEPTH',-1)
  for _,source in ipairs({3,4}) do
    reaper.CreateTrackSend(tracks[source],tracks[1])
    reaper.CreateTrackSend(tracks[source],tracks[5])
  end
  reaper.SetOnlyTrackSelected(tracks[1])
  reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0),'D_VOL',0)
  reaper.SetEditCurPos(1,false,false)
  reaper.UpdateArrange()
  assert(reaper.ReaWeb_Open(root..'index.html')>0,reaper.ReaWeb_GetLastError())
end,debug.traceback)
if not ok then local f=io.open(root..'launcher-error.txt','w');f:write(err);f:close() end
''', encoding='utf-8')
(root / 'app.js').write_text(r'''
(async()=>{
 const checks=[],metrics={},streams=[],sleep=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(value,name)=>{if(!value)throw Error(name);checks.push(name);};
 const until=async(fn,name)=>{const deadline=performance.now()+12000;while(performance.now()<deadline){if(await fn())return;await sleep(20);}throw Error('Timeout: '+name);};
 const fields=['rmsMomentary','rmsIntegrated','lufsMomentary','lufsShortTerm','lufsIntegrated','processedSeconds'];
 const values=()=>streams.map(s=>{const p=s.latest();return p?reaper.audio.decodeMeter(p.data,s.info.channels):null;});
 const snapshot=()=>values().map(v=>v&&Object.fromEntries(fields.map(k=>[k,v[k]])));
 try{
  await reaper.lifecycle.ready;
  await reaper.fs.writeText('progress.txt','ready',{overwrite:true});
  metrics.extensionVersion=(await reaper.debug.getDiagnostics()).version;
  metrics.reaperVersion=await reaper.GetAppVersion();
  const tracks=[];for(let i=0;i<5;i++)tracks.push(await reaper.GetTrack(0,i));
  const accessor=await reaper.CreateTrackAudioAccessor(tracks[0]);
  try{
   metrics.emptyAccessor=[];
   for(let i=0;i<3;i++)metrics.emptyAccessor.push({changed:await reaper.AudioAccessorValidateState(accessor),
    start:await reaper.GetAudioAccessorStartTime(accessor),end:await reaper.GetAudioAccessorEndTime(accessor),
    result:await reaper.GetAudioAccessorSamples(accessor,48000,2,1,256,new Float64Array(512))});
  }finally{await reaper.DestroyAudioAccessor(accessor);}
  await reaper.fs.writeText('progress.txt','empty accessor checked',{overwrite:true});
  for(const [i,aggregate] of [[0,true],[1,true],[4,true],[2,false]]){
   const source=i===0?'selected-track':'track:'+await reaper.GetTrackGUID(tracks[i]);
   streams.push(await reaper.audio.openStream('meter',{source,aggregate,updateRate:30}));
  }
  await reaper.OnPlayButton();
  await reaper.fs.writeText('progress.txt','playing',{overwrite:true});
  await until(()=>values().every(v=>v?.processedSeconds>=1),'meter startup');
  const revision=await reaper.GetProjectStateChangeCount(0);
  const accumulate=async(name,seconds)=>{
   let previous=values().map(v=>v?.processedSeconds||0);
   await until(()=>{
    const v=values();
    v.forEach((m,i)=>{if(m){if(m.processedSeconds+.001<previous[i]){
     metrics.reset={previous,current:v.map(m=>m?.processedSeconds),timestamps:streams.map(s=>s.latest()?.timestamp)};
     throw Error(name+' history reset: '+i);
    }previous[i]=m.processedSeconds;}});
    return v.every(m=>m?.processedSeconds>=seconds);
   },name);
   const v=values();metrics[name]=snapshot();
   await reaper.fs.writeText('progress.txt',name,{overwrite:true});
   check(v.every(m=>fields.every(k=>Number.isFinite(m[k]))),name+' finite RMS/LUFS');
   check(v.every(m=>fields.slice(0,-1).every(k=>m[k]>-60)),name+' valid RMS/LUFS');
  };
  await accumulate('empty sources',6);
  check(await reaper.GetProjectStateChangeCount(0)===revision,'empty validation preserves project revision');
  const initial=values();
  for(const i of [0,1])for(const k of fields.slice(0,-1))check(Math.abs(initial[i][k]-initial[3][k])<.15,'aggregate/reference '+i+' '+k);
  check(Math.abs(initial[2].rmsMomentary-initial[3].rmsMomentary-20*Math.log10(1.5))<.15,'own media plus upstream RMS');
  const reset=async(name,action,indices=[0,1,2,3])=>{
   const before=values().map(v=>v.processedSeconds);
   await action();
   await until(()=>indices.every(i=>values()[i]?.processedSeconds<before[i]-.5),name+' reset');
   await sleep(200);
   await accumulate(name,3.5);
  };
  let added;
  await reset('add media to empty upstream',async()=>{
   added=await reaper.AddMediaItemToTrack(tracks[3]);
   await reaper.SetMediaItemInfo_Value(added,'D_LENGTH',120);
   await reaper.SetMediaItemInfo_Value(added,'D_FADEINLEN',0);
   await reaper.SetMediaItemInfo_Value(added,'D_FADEOUTLEN',0);
   const take=await reaper.AddTakeToMediaItem(added);
   check(await reaper.SetMediaItemTake_Source(take,await reaper.PCM_Source_CreateFromFile(@OWNPATH@)),'assign new media source');
   await reaper.UpdateItemInProject(added);
   await reaper.Undo_OnStateChange('Add upstream media');
   await reaper.UpdateArrange();
  });
  check(Math.abs(values()[0].rmsMomentary-initial[0].rmsMomentary-20*Math.log10(1.5))<.15,'new upstream media contributes RMS');
  await reset('remove upstream media',async()=>{
   check(await reaper.DeleteTrackMediaItem(tracks[3],added),'delete upstream media');
   await reaper.Undo_OnStateChange('Remove upstream media');
  });
  check(Math.abs(values()[0].rmsMomentary-initial[0].rmsMomentary)<.15,'empty upstream restores RMS');
  await reset('edit existing media',async()=>{
   const item=await reaper.GetTrackMediaItem(tracks[2],0);
   await reaper.SetMediaItemInfo_Value(item,'D_VOL',.5);
   await reaper.UpdateItemInProject(item);
   await reaper.Undo_OnStateChange('Edit source volume');
   await reaper.UpdateArrange();
  });
  check(Math.abs(values()[0].rmsMomentary-initial[0].rmsMomentary+20*Math.log10(2))<.15,'existing media edit contributes RMS');
  await reset('route mute',()=>reaper.SetTrackSendInfo_Value(tracks[2],0,1,'B_MUTE',1),[2]);
  check(Math.abs(values()[2].rmsMomentary-values()[3].rmsMomentary)<.15,'muted receive removes upstream RMS');
  await reset('route unmute',()=>reaper.SetTrackSendInfo_Value(tracks[2],0,1,'B_MUTE',0),[2]);
  await reset('seek',()=>reaper.SetEditCurPos(70,false,true));
  await reaper.OnStopButton();await sleep(200);
  const stopped=values().map(v=>v.processedSeconds);await sleep(500);
  check(values().every((v,i)=>v.processedSeconds===stopped[i]),'stopped meters hold history');
  await reset('playback restart',()=>reaper.OnPlayButton());
  await reaper.OnStopButton();
  await Promise.all(streams.map(s=>s.close()));
  await until(async()=>!(await reaper.stream.getDiagnostics()).streams.length,'stream cleanup');
  check(true,'all meter streams released');
  await reaper.fs.writeText('result.json',JSON.stringify({passed:true,checks,metrics}));
 }catch(error){
  metrics.last=snapshot();
  await Promise.all(streams.map(s=>s.close().catch(()=>{})));
  await reaper.fs.writeText('result.json',JSON.stringify({passed:false,checks,metrics,error:String(error),stack:error.stack}));
 }
})();
'''.replace('@OWNPATH@', json.dumps(str(root / 'own.wav'))), encoding='utf-8')
command = [str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')]
with (root / 'process.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen(command, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 180
        while not (root / 'result.json').exists() and not (root / 'launcher-error.txt').exists() and process.poll() is None and time.monotonic() < deadline:
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
