from pathlib import Path
import json,sys,time
from jsfx_probe_client import call

root=Path(sys.argv[1]);result={}
fx=0 if '--resume' in sys.argv else call(root,'quietFixture');time.sleep(.5)
call(root,'save');before=call(root,'history')
assert before['dirty']==0,before
for i in range(12):
 call(root,'bootstrap',fx=fx);time.sleep(.1)
 after=call(root,'history');assert after==before,(i,before,after)
result['cleanReconnect']={'cycles':12,'before':before,'after':after}
call(root,'quietEdit');assert call(root,'undo')==1
time.sleep(.2);call(root,'save');before=call(root,'history')
assert before['dirty']==0 and before['redo']=='Quiet fixture pan',before
for i in range(12):
 call(root,'bootstrap',fx=fx);time.sleep(.1)
 after=call(root,'history');assert after==before,(i,before,after)
result['redoPreserved']={'cycles':12,'before':before,'after':after}
assert call(root,'redo')==1
result['redoExecuted']=True
(root/'quiet-results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result),flush=True)
