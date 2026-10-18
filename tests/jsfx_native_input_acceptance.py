"""Owned MIDI cleanup against an opt-in REAPER profile after setup/hubSetup."""
from pathlib import Path
import json, sys, time, uuid
from jsfx_probe_client import call

p=Path(sys.argv[1])
def hub(method,window=7,**payload):
    return call(p,'hubCall',index=0,window=window,method=method,payload=payload)
def invoke(name,args,window=7):
    op=hub('invokeCommand',window,id=name,args=args,requestId=uuid.uuid4().hex)
    for _ in range(200):
        op=hub('getOperation',window,operationId=op['operationId'])
        if op['status'] not in ('queued','running'):
            assert op['status']=='succeeded',op
            return op['result']
        time.sleep(.02)
    raise TimeoutError(op)
def count(expected):
    for _ in range(50):
        value=invoke('notes',{})
        if value==expected:return
        time.sleep(.02)
    raise AssertionError((value,expected))
observer=call(p,'midiObserver')
assert observer>=1
before=call(p,'midiEvents',fx=observer)
assert invoke('note',dict(channel=0,note=60,velocity=96),7)==1
assert invoke('note',dict(channel=0,note=60,velocity=96),8)==0
assert invoke('note',dict(channel=0,note=64,velocity=96),8)==1
count(2)
hub('releaseCommands',7);count(1)
hub('releaseCommands',7);count(1)
hub('releaseCommands',8);count(0)
for _ in range(12):assert invoke('note',dict(channel=0,note=60,velocity=96))==1
count(1);hub('releaseCommands');count(0)
time.sleep(.2)
after=call(p,'midiEvents',fx=observer)
delta=[b-a for a,b in zip(before,after)]
assert delta==[14,14,0,0],delta
(p/'midi-input-results.json').write_text(json.dumps(dict(before=before,after=after,delta=delta),indent=2))
print('PASS: isolated owners, idempotent release, retrigger, 14 paired note events, no global clear')
