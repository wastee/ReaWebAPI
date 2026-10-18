from pathlib import Path
import json,sys,time,socket,struct,hashlib,zlib
from urllib.parse import urlsplit
from jsfx_probe_client import call
root=Path(sys.argv[1])
def invoke(method,**payload):return call(root,'hubCall',method=method,payload=payload)
def wait(op):
 for _ in range(500):
  result=invoke('getOperation',operationId=op['operationId'])
  if result['status'] in ('succeeded','failed','cancelled'):return result
  time.sleep(.02)
 raise TimeoutError(op)
def connection(info):
 url=urlsplit(info['transport']['url']);s=socket.create_connection((url.hostname,url.port),timeout=5)
 s.sendall((f'GET {url.path} HTTP/1.1\r\nHost: {url.netloc}\r\nOrigin: http://jsfx-probe.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
 header=b''
 while not header.endswith(b'\r\n\r\n'):
  b=s.recv(1);assert b;header+=b
 assert b'101 Switching Protocols' in header,header
 return s

def exact(s,n):
 result=b''
 while len(result)<n:
  part=s.recv(n-len(result));assert part;result+=part
 return result

def chunk(s,info,seq,offset,data,upload):
 length=len(data) if upload else data
 packet=bytearray(104)+(data if upload else b'');packet[:6]=bytes([82,87,82,1,1 if upload else 2,1]);packet[32:96]=info['transferId'].encode()
 struct.pack_into('<QQI',packet,8,seq,offset,length);struct.pack_into('<I',packet,96,zlib.crc32(data) if upload else 0)
 mask=b'\x11\x22\x33\x44';n=len(packet)
 frame=b'\x82'+(bytes([128|n]) if n<126 else b'\xfe'+struct.pack('!H',n) if n<65536 else b'\xff'+struct.pack('!Q',n))
 s.sendall(frame+mask+bytes(v^mask[i%4] for i,v in enumerate(packet)))
 head=exact(s,2);n=head[1]&127
 if n==126:n=struct.unpack('!H',exact(s,2))[0]
 elif n==127:n=struct.unpack('!Q',exact(s,8))[0]
 response=exact(s,n);assert response[5]==2,response[104:]
 assert struct.unpack_from('<I',response,96)[0]==zlib.crc32(response[104:])
 return response[104:]

def download(id):
 info=invoke('openResourceDownload',id=id)
 while info.get('pending'):
  time.sleep(.02);info=invoke('openResourceDownload',id=id,transferId=info['transferId'])
 s=connection(info);result=b''
 for seq,offset in enumerate(range(0,info['totalBytes'],5000),1):
  result+=chunk(s,info,seq,offset,min(5000,info['totalBytes']-offset),False)
 assert hashlib.sha256(result).hexdigest()==info['checksum']
 s.close();invoke('closeResourceDownload',transferId=info['transferId'])
 return info,result

def upload(id,format,data,revision,request,metadata):
 info=invoke('beginResourceUpload',id=id,format=format,totalBytes=len(data),metadata=metadata,requestId=request)
 s=connection(info)
 for seq,offset in enumerate(range(0,len(data),5000),1):chunk(s,info,seq,offset,data[offset:offset+5000],True)
 checksum=hashlib.sha256(data).hexdigest()
 op=invoke('commitResourceUpload',transferId=info['transferId'],expectedRevision=revision,checksum=checksum,requestId=request+'-commit');s.close()
 return wait(op)

initial,original=download('asset');assert original==bytes([1,2,3]),original
metadata={'name':'wave\u0000bytes','rate':48000,'channels':2}
data=bytes(range(256))*96
commit=upload('asset','bytes',data,initial['revision'],'bytes-1',metadata);assert commit['status']=='succeeded',commit
info,actual=download('asset');assert actual==data and info['metadata']==metadata,(len(actual),info)
assert call(root,'undo')==1;time.sleep(.3)
undone,actual=download('asset');assert actual==original and undone['metadata']=={},undone
assert call(root,'redo')==1;time.sleep(.3)
redone,actual=download('asset');assert actual==data and redone['metadata']==metadata,redone
conflict=upload('asset','bytes',b'bad',initial['revision'],'conflict',{})
assert conflict['status']=='failed' and conflict['error']['code']=='CONFLICT',conflict
info,actual=download('asset');assert actual==data
floating,original_float=download('table');assert original_float==b''
floats=struct.pack('<'+str(6000)+'f',*[i/6000-.5 for i in range(6000)])
float_commit=upload('table','float32',floats,floating['revision'],'float-1',{'layout':'mono'})
assert float_commit['status']=='succeeded',float_commit
float_info,actual_float=download('table');assert actual_float==floats
invalid=upload('table','float32',struct.pack('<f',float('nan')),float_info['revision'],'invalid-float',{})
assert invalid['status']=='failed' and invalid['error']['code']=='INVALID_ARGUMENT',invalid
_,preserved=download('table');assert preserved==floats
result={'bytes':len(data),'metadata':metadata,'commit':commit,'undoRevision':undone['revision'],'redoRevision':redone['revision'],'conflict':conflict,'float32Bytes':len(floats),'invalidFloat':invalid}
(root.parent/(root.name+'-resources.json')).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
