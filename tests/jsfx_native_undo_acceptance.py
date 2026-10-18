from pathlib import Path
import json,sys,time
from jsfx_probe_client import call
root=Path(sys.argv[1])
def invoke(method,**payload):return call(root,'hubCall',method=method,payload=payload)
def state():
 for _ in range(100):
  value=invoke('getDataState',id='curve')
  if value['available']:return value
  time.sleep(.02)
 raise TimeoutError('state')
def wait(op):
 for _ in range(100):
  value=invoke('getOperation',operationId=op['operationId'])
  if value['status'] in ('succeeded','failed','cancelled'):return value
  time.sleep(.02)
 raise TimeoutError(op)

results=[]
for cycle in range(12):
 time.sleep(1.2)
 before=state();values=[.11+cycle/100,.23,.3456789012345678,.91]
 operation=wait(invoke('setDataState',id='curve',value=values,expectedRevision=before['revision'],requestId='undo-check-'+str(cycle)))
 assert operation['status']=='succeeded',operation
 assert call(root,'undo')==1
 time.sleep(.15);undone=state();assert undone['value']==before['value'],(cycle,before,undone)
 assert call(root,'redo')==1
 time.sleep(.15);redone=state();assert redone['value']==values,(cycle,values,redone)
 results.append({'before':before,'undo':undone,'redo':redone})
 print('cycle',cycle+1,'passed',flush=True)
(root.parent/(root.name+'-hub-undo.json')).write_text(json.dumps(results,indent=2),encoding='utf-8')
