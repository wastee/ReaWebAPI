"""Compare the built-in meter with the supplied Cockos JSFX in isolated REAPER."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reaper', type=Path, required=True)
parser.add_argument('--extension', type=Path, required=True)
parser.add_argument('--jsfx', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rate', type=int, default=48000)
parser.add_argument('--channels', type=int, choices=[2, 6, 32], default=2)
parser.add_argument('--audio-config', type=Path)
parser.add_argument('--analysis-driver', type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / 'UserPlugins').mkdir()
(root / 'Effects' / 'analysis').mkdir(parents=True)
(root / 'empty-vst').mkdir()
reference = args.jsfx.read_text(encoding='utf-8')
reference = reference.replace('options:no_meter', '\n'.join([
    'slider37:ref_frames=0<0,10000000,1>-Reference frames',
    'slider38:ref_tpclips_l=0<0,10000000,1>-Reference TP clips L',
    'slider39:ref_tpclips_r=0<0,10000000,1>-Reference TP clips R',
    'slider40:ref_tp_l=0<0,100,0.000001>-Reference TP L',
    'slider41:ref_tp_r=0<0,100,0.000001>-Reference TP R',
    'slider42:ref_lra_low=-100<-100,20,0.000001>-Reference LRA low',
    'slider43:ref_lra_high=-100<-100,20,0.000001>-Reference LRA high',
    'slider44:ref_max_m=-100<-100,20,0.000001>-Reference max LUFS-M',
    'slider45:ref_max_s=-100<-100,20,0.000001>-Reference max LUFS-S',
    'options:no_meter']))
reference = reference.replace('@block', '@block\ntest_index=(play_position*srate+0.5)|0;')
reference = reference.replace('function Reset()\n(', 'function Reset()\n(\n  test_frames=0;')
reference = reference.replace('@sample', '@sample\n(play_state&1) && test_index >= srate && test_index < 7*srate ? (\n test_index == srate ? Reset();')
reference = reference.replace('@gfx 300 424', 'test_frames+=1;\n);\ntest_index+=1;\nref_frames=test_frames;\nref_tpclips_l=ch0.clips; ref_tpclips_r=ch1.clips;\nref_tp_l=ch0.hipkval; ref_tp_r=ch1.hipkval;\nref_lra_low=lra_db_lo; ref_lra_high=lra_db_hi;\nref_max_m=lufs_m_sum_max > 0 ? -0.691+log(lufs_m_sum_max)*10/log(10) : -100;\nref_max_s=lufs_s_sum_max > 0 ? -0.691+log(lufs_s_sum_max)*10/log(10) : -100;\n@gfx 300 424')
(root / 'Effects' / 'analysis' / 'loudness_meter').write_text(reference, encoding='utf-8')
extension = root / 'UserPlugins' / args.extension.name
shutil.copy2(args.extension, extension)
if sys.platform == 'darwin':
    subprocess.run(['codesign', '--force', '--sign', '-', str(extension)], check=True)
if sys.platform.startswith('linux'):
    for helper in args.extension.parent.glob('reawebapi-webview-*'):
        shutil.copy2(helper, root / 'UserPlugins' / helper.name)
config = '[REAPER]\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\naudioclosestop=0\naudiocloseinactive=0\n'
config += f'vst_scan=0\nvstfullstate=49989\nvstpath={root / "empty-vst"}\nvstpath64={root / "empty-vst"}\n'
if sys.platform == 'win32':
    config += f'[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate={args.rate}\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_in=0\nwaveout_nch_out=2\n'
elif sys.platform == 'darwin':
    config = config.replace('[REAPER]', '[reaper]')
    if args.audio_config:
        import configparser
        source = configparser.ConfigParser(strict=False, interpolation=None)
        source.read(args.audio_config, encoding='utf-8-sig')
        section = next(name for name in source.sections() if name.lower() == 'reaper')
        config += ''.join(f'{key}={value}\n' for key, value in source[section].items() if key.startswith('coreaudio'))
    config += 'hasrecentlyopened=1\n[audioconfig]\nmode=0\n'
else:
    config += f'linux_audio_mode=3\nlinux_audio_srate={args.rate}\nlinux_audio_bsize=512\nlinux_audio_nch_out=2\n'
if args.audio_config and sys.platform != 'darwin':
    import configparser
    source = configparser.ConfigParser(strict=False, interpolation=None)
    source.read(args.audio_config, encoding='utf-8-sig')
    config = config.split('[audioconfig]')[0] + '[audioconfig]\n' + ''.join(f'{k}={v}\n' for k, v in source['audioconfig'].items())
(root / 'reaper.ini').write_text(config, encoding='utf-8')
names = ['silence', 'sine', 'near-zero', 'impulse', 'intersample', 'clipping', 'pink', 'levels']
for name in names:
    pcm = bytearray()
    random = 17
    b0 = b1 = b2 = 0
    for i in range(args.rate * 12):
        tone = math.sin(2 * math.pi * 1000 * i / args.rate)
        if name == 'silence':
            value = 0
        elif name == 'sine':
            value = .5 * tone
        elif name == 'near-zero':
            value = .999 * tone
        elif name == 'impulse':
            value = 1 if i == args.rate else 0
        elif name == 'intersample':
            value = 1.1 * math.sin(math.pi * i / 2 + math.pi / 4)
        elif name == 'clipping':
            value = 1.5 * tone
        elif name == 'levels':
            value = (.1 if i < args.rate * 3 else .4) * tone
        else:
            random = (random * 1664525 + 1013904223) & 0xffffffff
            white = random / 0xffffffff * 2 - 1
            b0 = .99765 * b0 + white * .0990460
            b1 = .963 * b1 + white * .2965164
            b2 = .57 * b2 + white * 1.0526913
            value = (b0 + b1 + b2 + white * .1848) * .05
        pcm.extend(struct.pack('<' + 'f' * args.channels, *([value if ch < 2 else value / (ch + 1) for ch in range(args.channels)])))
    fmt = struct.pack('<HHIIHH', 3, args.channels, args.rate, args.rate * args.channels * 4, args.channels * 4, 32)
    (root / f'{name}.wav').write_bytes(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(pcm)) + pcm)
(root / 'index.html').write_text('<!doctype html><title>Loudness acceptance</title><pre>Native/Cockos loudness comparison</pre><script src="app.js"></script>')
(root / 'launch.lua').write_text(r'''
local root = debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])')
local ok, err = xpcall(function()
  local mappings = {}
  reaper.GetSetProjectInfo(0,'PROJECT_SRATE',@RATE@,true)
  reaper.GetSetProjectInfo(0,'PROJECT_SRATE_USE',1,true)
  for i,name in ipairs({'silence','sine','near-zero','impulse','intersample','clipping','pink','levels'}) do
    reaper.InsertTrackAtIndex(i-1,false)
    local t = reaper.GetTrack(0,i-1)
    reaper.SetMediaTrackInfo_Value(t,'I_NCHAN',@CHANNELS@)
    reaper.GetSetMediaTrackInfo_String(t,'P_NAME',name,true)
    local item = reaper.AddMediaItemToTrack(t)
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',12)
    reaper.SetMediaItemInfo_Value(item,'D_POSITION',1)
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0)
    reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    local take = reaper.AddTakeToMediaItem(item)
    reaper.SetMediaItemTake_Source(take,reaper.PCM_Source_CreateFromFile(root..name..'.wav'))
    local fx = reaper.TrackFX_AddByName(t,'JS: analysis/loudness_meter',false,-1)
    assert(fx >= 0,'Cockos meter missing')
    local params = {}
    for p=0,reaper.TrackFX_GetNumParams(t,fx)-1 do
      local _,label=reaper.TrackFX_GetParamName(t,fx,p)
      params[#params+1] = string.format('%q:%d',label,p)
      if label=='Peak' then reaper.TrackFX_SetParam(t,fx,p,1) end
      if label=='RMS momentary' or label=='RMS integrated' or label=='Output loudness values as automation' then reaper.TrackFX_SetParam(t,fx,p,1) end
    end
    mappings[#mappings+1] = '{'..table.concat(params,',')..'}'
  end
  local meta=assert(io.open(root..'fx-params.json','w'));meta:write('['..table.concat(mappings,',')..']');meta:close()
  reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0),'D_VOL',0)
  reaper.SetEditCurPos(0,false,false)
  reaper.UpdateArrange()
  assert(reaper.ReaWeb_Open(root..'index.html')>0,reaper.ReaWeb_GetLastError())
end,debug.traceback)
if not ok then local f=io.open(root..'launcher-error.txt','w');f:write(err);f:close() end
'''.replace('@RATE@', str(args.rate)).replace('@CHANNELS@', str(args.channels)), encoding='utf-8')
(root / 'app.js').write_text(r'''
(async()=>{
 const C=@CHANNELS@,B=7*C;
 const checks=[],metrics={},sleep=ms=>new Promise(r=>setTimeout(r,ms)),streams=[];
 const check=(value,name)=>{if(!value)throw Error(name);checks.push(name);};
 const until=async(test,name)=>{for(let i=0;i<600;i++){if(test())return;await sleep(20);}throw Error('Timeout: '+name);};
 try{
  await reaper.lifecycle.ready;
  metrics.version=(await reaper.debug.getDiagnostics()).version;
  const mappings=JSON.parse(await reaper.fs.readText('fx-params.json'));
  const tracks=[];
  for(let i=0;i<8;i++){
   const t=await reaper.GetTrack(0,i);tracks.push(t);
   streams.push(await reaper.audio.openStream('meter',{source:'track:'+await reaper.GetTrackGUID(t),updateRate:[10,30,60][i%3]}));
  }
  await reaper.OnPlayButton();
  await Promise.all(tracks.map((t,i)=>reaper.TrackFX_SetParam(t,0,mappings[i]['Reset'],1)));
  try { await until(()=>streams.every(s=>s.latest()?.data[B+11]>7.5),'continuous meter history'); }
  catch(error) { metrics.streamHistory=streams.map(s=>({info:s.info,data:Array.from(s.latest()?.data||[])}));throw error; }
  const fields=['Peak/True peak dB (output)','RMS-M (output)','RMS-I (output)','LUFS-M (output)','LUFS-S (output)','LUFS-I (output)','LRA (output)','Reference frames','Reference TP clips L','Reference TP clips R','Reference TP L','Reference TP R','Reference LRA low','Reference LRA high','Reference max LUFS-M','Reference max LUFS-S'];
  const names=['silence','sine','near-zero','impulse','intersample','clipping','pink','levels'];
  const snapshots=streams.map(s=>Array.from(s.latest().data));
  const references=await Promise.all(tracks.map(async(t,i)=>Object.fromEntries(await Promise.all(fields.map(async name=>[name,(await reaper.TrackFX_GetParam(t,0,mappings[i][name]))[0]])))));
  for(let i=0;i<8;i++){
   const output=references[i],data=snapshots[i],db=value=>value>0?20*Math.log10(value):-150;
   metrics[names[i]]={rate:streams[i].info.sampleRate,native:data,cockos:output};
   check(output['Reference frames']===6*streams[i].info.sampleRate,'exact Cockos reference frames '+names[i]);
   check(data.length===13*C+14,'extended meter layout '+names[i]);
   check(data[B+9]===Math.max(...data.slice(5*C,6*C))&&data[B+10]===Math.max(...data.slice(6*C,7*C)),'channel/global peak consistency '+names[i]);
   check(Math.abs(data[B+6]-(data[B+13]-data[B+12]))<1e-5,'LRA bounds '+names[i]);
  }
  const before=streams[1].latest().data[B+11];await reaper.OnStopButton();await sleep(300);
  const stopped=streams[1].latest().data[B+11];await sleep(250);
  check(stopped>=before&&streams[1].latest().data[B+11]===stopped,'stopped history holds');
  await reaper.audio.resetMeter(streams[1].info.name);
  await until(()=>streams[1].latest().data[B+11]===0,'manual reset');check(streams[1].latest().data[B+9]===0,'maxima reset');
  await Promise.all(streams.map(s=>s.close()));
  await reaper.SetMediaTrackInfo_Value(tracks[0],'I_NCHAN',2);
  await reaper.SetMediaTrackInfo_Value(tracks[0],'I_FOLDERDEPTH',1);
  await reaper.SetMediaTrackInfo_Value(tracks[1],'I_FOLDERDEPTH',-1);
  const aggregate=await reaper.audio.openStream('meter',{source:'track:'+await reaper.GetTrackGUID(tracks[0]),aggregate:true});
  const spectrum=await reaper.audio.openStream('spectrum',{source:'track:'+await reaper.GetTrackGUID(tracks[0]),aggregate:true});
  check(aggregate.info.channels===C,'aggregate preserves upstream channel width');
  check(spectrum.info.channels===2,'other analysis remains stereo');
  await reaper.SetEditCurPos(1,false,false);await reaper.OnPlayButton();
  await until(()=>aggregate.latest()?.data[B+11]>.5,'multichannel aggregate PCM');
  const decoded=reaper.audio.decodeMeter(aggregate.latest().data,C);
  for(let ch=0;ch<C;ch++){
   check(Math.abs(decoded.channelMaxSamplePeak[ch]-(ch<2?.5:.5/(ch+1)))<.001,'aggregate channel '+ch+' retained');
   check(decoded.sampleClipCount[ch]===0n,'exact bigint channel count '+ch);
  }
  let layoutClosed=false;aggregate.on('close',e=>{layoutClosed=e.code==='UNSUPPORTED_FORMAT';});
  await reaper.SetMediaTrackInfo_Value(tracks[1],'I_NCHAN',C===2?6:2);
  await until(()=>layoutClosed,'channel change closes immutable descriptor');
  check(layoutClosed,'channel change requires reopening');
  await spectrum.close();await reaper.OnStopButton();
  await reaper.fs.writeText('result.json',JSON.stringify({passed:true,checks,metrics}));
 }catch(error){await reaper.fs.writeText('result.json',JSON.stringify({passed:false,checks,metrics,error:String(error),diagnostics:await reaper.debug.getDiagnostics()}));}
})();
'''.replace('@CHANNELS@', str(args.channels)), encoding='utf-8')
with (root / 'reaper.log').open('w') as log:
    process = subprocess.Popen([str(args.reaper), '-newinst', '-cfgfile', str(root / 'reaper.ini'), '-new', '-ignoreerrors', '-nosplash', str(root / 'launch.lua')], stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 80
        while time.monotonic() < deadline and process.poll() is None and not (root / 'result.json').exists() and not (root / 'launcher-error.txt').exists():
            time.sleep(.2)
        result = json.loads((root / 'result.json').read_text()) if (root / 'result.json').exists() else dict(passed=False, error=(root / 'launcher-error.txt').read_text() if (root / 'launcher-error.txt').exists() else 'No report')
        if result.get('passed'):
            try:
                for name in names:
                    actual = json.loads(subprocess.check_output([str(args.analysis_driver.resolve()), str(root / f'{name}.wav'), str(args.rate), str(args.channels), str(6 * args.rate)], text=True))
                    reference = result['metrics'][name]['cockos']
                    result['metrics'][name]['offlineNative'] = actual
                    for offset, label in [(0, 'RMS-M (output)'), (1, 'RMS-I (output)'), (3, 'LUFS-M (output)'), (4, 'LUFS-S (output)'), (5, 'LUFS-I (output)'), (6, 'LRA (output)'), (7, 'Reference max LUFS-M'), (8, 'Reference max LUFS-S'), (12, 'Reference LRA low'), (13, 'Reference LRA high')]:
                        index = 7 * args.channels + offset
                        value = actual[index] if actual[index] is not None else -100
                        assert abs(value - reference[label]) < .0005, (name, label, value, reference[label])
                        result['checks'].append(f'exact PCM {label} {name}')
                    for channel, label in enumerate(('L', 'R')):
                        assert abs(actual[6 * args.channels + channel] - reference[f'Reference TP {label}']) < 2e-6, (name, 'True Peak', actual, reference)
                        assert actual[4 * args.channels + channel] == reference[f'Reference TP clips {label}'], (name, 'True Peak clips', actual, reference)
                        result['checks'].append(f'exact PCM True Peak and clips {name} {label}')
                    if name == 'intersample':
                        assert actual[3 * args.channels] == actual[3 * args.channels + 1] == 0 and actual[4 * args.channels] > 0 and actual[4 * args.channels + 1] > 0
                        result['checks'].append('sample clips separate from inter-sample clips')
            except (AssertionError, subprocess.CalledProcessError) as error:
                result['passed'] = False
                result['error'] = str(error)
            (root / 'result.json').write_text(json.dumps(result), encoding='utf-8')
        print(json.dumps(result), flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
raise SystemExit(0 if result['passed'] else 1)
