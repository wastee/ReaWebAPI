"""Opt-in cached JSFX host-path comparison in the existing isolated Windows fixture."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'latency-results/2026-10-22-cached'
spec = importlib.util.spec_from_file_location('compare', ROOT / 'tests/startup_latency_compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
compare.OUT = OUT
compare.b.BASE = OUT

OBSERVE = '''
local function observer(d)
  local result={}
  for _,key in ipairs({'hook','notification','scan','prepareFloating','inspect','match','identifyControls','resolveOwnership','hitTest','prepareEditor','createHost','conceal','restoreSize','cloak','hideControls'}) do
    local value=(d or ''):match('"'..key..'":({[^}]+})') or '{}'
    result[key..'Ms']=number(value,'ms') or 0
    result[key..'Calls']=number(value,'calls') or 0
  end
  return result
end
local function difference(after,before)
  local result={}
  for key,value in pairs(after) do result[key]=value-(before[key] or 0) end
  return result
end
'''

def configure(arm, functional=False):
    compare.configure(arm, functional)
    runner = compare.b.PORT / 'Scripts/runner.lua'
    source = runner.read_text(encoding='utf-8')
    source = source.replace('local co=coroutine.create(function()', OBSERVE+'\nlocal co=coroutine.create(function()')
    source = source.replace('local start=r.time_precise();row.requestClockSeconds=start',
        'local observerBefore=observer(d)\n      local start=r.time_precise();row.requestClockSeconds=start')
    source = source.replace('local done=false;local showStart;', '''
      if id then
        local at=r.time_precise()
        row.apiObserver=difference(observer(diag(id)),observerBefore)
        row.apiDiagnosticsMs=(r.time_precise()-at)*1000
      end
      local done=false;local showStart;''')
    source = source.replace('row.ok=done;row.pollCalls=',
        'row.toDetectionObserver=difference(observer(d),observerBefore)\n      row.ok=done;row.pollCalls=')
    runner.write_text(source, encoding='utf-8')
    identity = OUT / (arm+'-identity.json')
    data = json.loads(identity.read_text())
    data['sourceCommit'] = subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()
    data['version'] = '0.3.8.34'
    data['runnerSHA256'] = hashlib.sha256(runner.read_bytes()).hexdigest()
    identity.write_text(json.dumps(data, indent=2), encoding='utf-8')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('arm')
    parser.add_argument('--run', required=True)
    parser.add_argument('--samples', type=int, default=33)
    parser.add_argument('--suite', choices=['cached','functional','validation'], default='cached')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    configure(args.arm, args.suite=='functional')
    def cases(suite, page):
        if suite == 'functional': return [dict(scenario='J2',page='1175',samples=1,cold=True)]
        if suite == 'validation': return [dict(scenario=s,page='1175',samples=args.samples,**opts) for s,opts in [
            ('J2',{}),('J3',{'mode':'chain'}),('J3',{'owner':'take'}),('J3',{'owner':'container'}),
            ('J5-switch',{'mode':'chain','instances':3})]]
        return [dict(scenario='J3',page='1175',samples=args.samples)]
    compare.b.cases = cases
    try: compare.b.run(args.run,args.suite,'1175',0,1)
    finally: compare.stop()
