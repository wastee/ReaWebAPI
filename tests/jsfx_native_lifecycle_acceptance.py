from pathlib import Path
import sys,json,time
from jsfx_probe_client import call
root=Path(sys.argv[1]);results={}
def run(op,**kw):
 r=call(root,op,**kw);results[op+str(kw)]=r
 print(op,str(kw),'ok',flush=True)
 return r
s=run('status') if '--resume' in sys.argv else run('setup',count=16)
assert all(x['valid'] for x in s)
if '--resume' not in sys.argv:
 device=run('device')
 if not device['running']:run('audioInit')
run('play');time.sleep(.4)
for kind in ['input','master','monitor','take','container']:
 s=run('chain',kind=kind);assert s[-1]['valid'],s[-1]
s=run('copy',index=0);assert s[-1]['valid'] and s[-1]['epoch']!=s[0]['epoch']
r=run('replace',index=1);assert all(r.values()),r
r=run('undoDelete',index=2);assert all(r.values()),r
r=run('projects');assert all(r.values()),r
run('enabled',index=0,value=False);run('enabled',index=0,value=True)
run('offline',index=1,value=True);s=run('status');assert not s[1]['valid'],s[1]
run('offline',index=1,value=False);time.sleep(.4)
s=run('status');assert s[1]['valid'],s[1]
run('seek',position=10);time.sleep(.4)
run('send',index=0,request=100,values=[1,2,3,4]);time.sleep(.2)
r=run('reply',index=0);assert r['ready'] and r['request']==100 and r['values']==[10],r
s=run('status');assert all(x['valid'] and x['errors']==0 for x in s),s
assert min(x['frames'] for x in s[:16])>0,s
(root.parent/(root.name+'-lifecycle.json')).write_text(json.dumps(results,indent=2))
print(json.dumps({'instances':len(s),'errors':sum(x['errors'] for x in s),'minFrames':min(x['frames'] for x in s[:16])}),flush=True)
