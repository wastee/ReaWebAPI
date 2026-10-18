from pathlib import Path
import json,sys,time,socket,struct
from urllib.parse import urlsplit
from jsfx_probe_client import call
root=Path(sys.argv[1])
def invoke(method,**payload):return call(root,'hubCall',method=method,payload=payload)
def wait(op):
 for _ in range(100):
  result=invoke('getOperation',operationId=op['operationId'])
  if result['status'] in ('succeeded','failed','cancelled'):return result
  time.sleep(.03)
 raise TimeoutError(op)
device=call(root,'device')
if not device['running']:call(root,'audioInit')
if not device['playState']&1:call(root,'play')
time.sleep(.2)
assert call(root,'hubSetup')==16
for _ in range(100):
 state=invoke('getDataState',id='curve')
 if state['available']:break
 time.sleep(.05)
assert state['available'],call(root,'hubStatus')
op=invoke('setDataState',id='curve',value=[.1,.2,.3,.4],expectedRevision=state['revision'],requestId='state-1')
assert invoke('setDataState',id='curve',value=[.1,.2,.3,.4],expectedRevision=state['revision'],requestId='state-1')['operationId']==op['operationId']
done=wait(op);assert done['status']=='succeeded',done
assert done['result']['value']==[.1,.2,.3,.4],done
assert invoke('findOperation',requestId='state-1')['operation']['operationId']==op['operationId']
conflict=wait(invoke('setDataState',id='curve',value=[0,0,0,0],expectedRevision=state['revision'],requestId='stale'))
assert conflict['error']['code']=='CONFLICT',conflict
cmd=wait(invoke('invokeCommand',id='sum',args={'a':.25,'b':.5},requestId='sum'))
assert cmd['result']==.75,cmd
opened=invoke('openStream',id='output',options={});info=opened['transport'];url=urlsplit(info['url'])
s=socket.create_connection((url.hostname,url.port),timeout=5)
s.sendall((f'GET {url.path} HTTP/1.1\r\nHost: {url.netloc}\r\nOrigin: http://jsfx-probe.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
header=b''
while not header.endswith(b'\r\n\r\n'):header+=s.recv(1)
assert b'101 Switching Protocols' in header,header
def exact(n):
 data=b''
 while len(data)<n:
  chunk=s.recv(n-len(data));assert chunk;data+=chunk
 return data
packets=[]
for _ in range(5):
 head=exact(2);length=head[1]&127
 if length==126:length=struct.unpack('!H',exact(2))[0]
 elif length==127:length=struct.unpack('!Q',exact(8))[0]
 data=exact(length);assert data[:4]==b'RWS\x02',data[:20]
 values=struct.unpack('<'+str((length-80)//4)+'f',data[80:])
 left=values[::2];peak=max(range(len(left)),key=left.__getitem__)
 hz=peak*info['sampleRate']/info['fftSize']
 assert abs(hz-440)<info['sampleRate']/info['fftSize'],(hz,info)
 assert .8<left[peak]<=1.01,(hz,left[peak])
 packets.append({'sequence':struct.unpack_from('<Q',data,8)[0],'firstSample':struct.unpack_from('<Q',data,48)[0],'frequency':hz,'amplitude':left[peak]})
 s.sendall(b'\x82\x81\x11\x22\x33\x44\x10')
s.close()
result={'state':done,'conflict':conflict,'command':cmd,'spectrum':packets,'status':call(root,'hubStatus')}
(root.parent/(root.name+'-hub-roundtrip.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='status'},indent=2))
