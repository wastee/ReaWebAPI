from pathlib import Path
import json,sys,time
from jsfx_probe_client import call
root=Path(sys.argv[1]);seconds=float(sys.argv[2]) if len(sys.argv)>2 else 60
assert seconds>=10
device=call(root,'device');assert device['running'],device
result={'device':device,'secondsPerMode':seconds,'instances':16,'modes':{}}
states=call(root,'setup',count=16);assert all(s['valid'] for s in states),states
time.sleep(2)
for mode in ['baseline','idle','active','backpressure','active']:
 call(root,'measureBegin',mode=mode)
 time.sleep(seconds)
 r=call(root,'measureEnd');assert r['processingMs'] and r['overflow']==0,r
 if mode=='active':
  assert r['publicationToNativeMs'] and r['publicationToNativeMs']['min']>=-1,r
 key=mode if mode not in result['modes'] else 'recovered'
 result['modes'][key]=r
 (root/'audio-timing-results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(key,json.dumps(r),flush=True)
 time.sleep(1)
