"""Controlled Windows comparison; only the existing disposable benchmark is touched."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, shutil, re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'latency-results/2026-10-22-optimization'
spec = importlib.util.spec_from_file_location('bench', ROOT / 'tests/startup_latency_windows.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
b.BASE = OUT

# Same probe in every arm. Detailed measures are logged separately to avoid the
# runtime's per-message preview limit. No timing from a different clock is added.
DETAIL = r'''
 const measures=performance.getEntriesByType('measure').filter(x=>x.name.startsWith('reaweb.')||x.name.startsWith('bench.')).map(x=>[x.name,x.startTime,x.duration]);
 await reaper.debug.log('LATENCY_DETAIL '+JSON.stringify({measures}));
'''
WRAP = r'''
  const rawService = reaper.host.service('jsfx');
  let tracing = true;
  const measured = async (kind, name, call) => {
    const start = performance.now();
    try { return await call(); }
    finally {
      if(tracing) performance.measure('bench.'+kind+'.'+name,{start,end:performance.now()});
      if(kind==='invoke'&&name==='present') tracing=false;
    }
  };
  const service = {
    invoke: (name,...args) => measured('invoke',name,()=>rawService.invoke(name,...args)),
    on: (name,...args) => measured('on',name,()=>rawService.on(name,...args))
  };
'''

def stop():
    import psutil
    for p in psutil.process_iter(['exe']):
        if p.info['exe'] and Path(p.info['exe']).resolve() == (b.PORT / 'reaper.exe').resolve():
            children=p.children(recursive=True);p.terminate();p.wait(10)
            _,alive=psutil.wait_procs(children,8)
            if alive: raise RuntimeError('Test browser still alive')

def configure(arm, functional=False):
    stop()
    ini=b.PORT/'reaper.ini'
    config=ini.read_text(encoding='utf-8')
    for key in ['audioclosestop','audiocloseinactive']:
        config=re.sub(r'^'+key+r'=.*\n?', '', config, flags=re.M)
    config=config.replace('[REAPER]', '[REAPER]\naudioclosestop=0\naudiocloseinactive=0',1)
    ini.write_text(config,encoding='utf-8')
    dll = OUT / 'inputs' / (arm + '.dll')
    sdk = OUT / 'inputs' / (arm + '.mjs')
    if not dll.exists() or not sdk.exists(): raise FileNotFoundError(arm)
    shutil.copy2(dll,b.PORT/'UserPlugins/reaper_reawebapi-x64.dll')
    shutil.copy2(ROOT/'tests/startup_latency_windows.lua',b.PORT/'Scripts/runner.lua')
    runner=b.PORT/'Scripts/runner.lua'
    text=runner.read_text(encoding='utf-8')
    text=text.replace('local co=coroutine.create(function()', '''
local function audioState()
  local state={}
  for _,key in ipairs({'MODE','IDENT_IN','IDENT_OUT','BSIZE','SRATE','BPS'}) do
    local ok,value=r.GetAudioDeviceInfo(key)
    if ok and key:sub(1,6)=='IDENT_' then
      state[key..'_bytesHex']=value:gsub('.',function(c) return string.format('%02x',string.byte(c)) end)
    else state[key]=ok and value or false end
  end
  return state
end
local co=coroutine.create(function()''')
    text=text.replace('local start=r.time_precise();row.requestClockSeconds=start',
                      'row.audioBefore=audioState()\n      local start=r.time_precise();row.requestClockSeconds=start')
    text=text.replace('\n      write(row,d)', '\n      row.audioAfter=audioState()\n      write(row,d)')
    # Failed sessions are no longer IsOpen(), but their diagnostic records still
    # explain the failure. Preserve them and continue the fixed sample schedule.
    text=text.replace('\n      write(row,d)', '''
      if not done then
        row.extensionError=r.ReaWeb_GetLastError()
        row.failedSessionDiagnostics={}
        for n=1,maxid+5 do
          local failed=diag(n)
          if failed:find('"stage":"failed"',1,true) then row.failedSessionDiagnostics[tostring(n)]=failed end
        end
      end
      write(row,d)''')
    text=text.replace("if not done then error('Sample timed out: '..row.traceId) end", '''
      if not done then
        for n=1,maxid+5 do r.ReaWeb_Close(n) end
        wait(.25)
      end''')
    runner.write_text(text,encoding='utf-8')
    if functional:
        runner=b.PORT/'Scripts/runner.lua'
        text=runner.read_text(encoding='utf-8').replace('wait(.25);d=diag(id)', '''
        local deadline=r.time_precise()+10
        repeat wait(.1);d=diag(id) until d:find('LATENCY_FUNCTIONAL',1,true) or r.time_precise()>deadline
        assert(d:find('LATENCY_FUNCTIONAL pass',1,true), '1175 functional validation failed: '..d)
        assert(math.abs(r.TrackFX_GetParamNormalized(track,fx,0)-1)<0.000001,'Host parameter not restored')
        ''')
        runner.write_text(text,encoding='utf-8')
    source=sdk.read_text(encoding='utf-8')
    assert source.count("const service = reaper.host.service('jsfx');")==1
    source=source.replace("const service = reaper.host.service('jsfx');",WRAP)
    probe=b.PROBE.replace(' marks.navigation=',DETAIL+'\n marks.navigation=')
    for dest in [b.PORT/'Effects/Latency/1175/.webui',b.PORT/'Effects/Latency/minimal/.webui',b.PORT/'Scripts/minimal']:
        (dest/'jsfx-webui.mjs').write_text(source,encoding='utf-8')
        (dest/'probe.js').write_text(probe,encoding='utf-8')
    # Keep real app and DSP identical to the first benchmark, with validation only
    # after application initialization. No user input or parameter writes here.
    app=(b.FX/'.webui/app.js').read_text(encoding='utf-8')
    if functional:
        app+=r'''
if(jsfx) {
  try {
    const check=(value,message)=>{if(!value)throw Error(message);};
    const original=jsfx.params.snapshot.parameters[0].normalized;
    await jsfx.params.set(0,.5);
    check(Math.abs((await jsfx.params.getState()).parameters[0].normalized-.5)<1e-8,'parameter write');
    await jsfx.params.set(0,original);
    check(Math.abs((await jsfx.params.getState()).parameters[0].normalized-original)<1e-8,'parameter restore');
    const old=jsfx.editor.size;
    await jsfx.editor.setSize(1035,239);
    check(jsfx.editor.size.width===1035&&jsfx.editor.size.height===239,'size write');
    await jsfx.editor.setSize(old.width,old.height);
    for(const theme of ['silver','black','ae']) {
      document.querySelector('.styles button[data-theme="'+theme+'"]').click();
      check(document.body.dataset.theme===theme,'theme '+theme);
    }
    await reaper.debug.log('LATENCY_FUNCTIONAL pass: parameter write/restore, editor resize/restore, three themes');
  } catch(error) {await reaper.debug.log('LATENCY_FUNCTIONAL FAIL '+String(error));throw error;}
}
'''
    app+='\nif(jsfx) { await reaper.debug.log("LATENCY_STATE "+JSON.stringify({snapshot:jsfx.params.snapshot,size:jsfx.editor.size})); await window.__latencyDone(); }\n'
    (b.PORT/'Effects/Latency/1175/.webui/app.js').write_text(app,encoding='utf-8')
    (OUT/(arm+'-identity.json')).write_text(json.dumps({
        'dllSHA256':hashlib.sha256(dll.read_bytes()).hexdigest(),
        'sdkSHA256':hashlib.sha256(sdk.read_bytes()).hexdigest(),
        'sdkInstrumentedSourceLF_SHA256':hashlib.sha256(source.encode()).hexdigest(),
        'sdkInstrumentedFileSHA256':hashlib.sha256((b.PORT/'Effects/Latency/1175/.webui/jsfx-webui.mjs').read_bytes()).hexdigest(),
        'sourceCommit':'189da631b29d57a50147e3044060aec7615e72f8',
        'idle':0,'audio':'WaveOut configuration retained; audioclosestop=0, audiocloseinactive=0; actual device captured per sample',
    },indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('arm');p.add_argument('--run');p.add_argument('--suite',default='compare')
    a=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    configure(a.arm,a.suite=='functional')
    original=b.cases
    def cases(suite,page):
        if suite=='compare':return [dict(scenario=s,page=p) for s,p in [('A2','minimal'),('J2','minimal'),('J2','1175'),('J3','1175')]]
        if suite=='functional':return [dict(scenario='J2',page='1175',samples=1,cold=True)]
        if suite=='1175':return [dict(scenario=s,page='1175') for s in ['J2','J3']]
        if suite=='1175-new':return [dict(scenario='J2',page='1175')]
        if suite=='validation':return [dict(scenario=s,page='1175',**opts) for s,opts in [
            ('J4',{}),('J6-new',{'mode':'chain'}),('J2',{'owner':'take'}),('J2',{'owner':'container'})]]
        return original(suite,page)
    b.cases=cases
    try:b.run(a.run or a.arm,a.suite,'1175',0,1)
    finally:stop()
