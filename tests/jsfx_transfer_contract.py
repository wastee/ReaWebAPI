"""Reliable binary chunks reuse the protected native Stream transport."""
import hashlib,json,socket,struct,subprocess,sys,zlib
from urllib.parse import urlsplit

def connect(info,origin):
    url=urlsplit(info['url']);sock=socket.create_connection((url.hostname,url.port),timeout=5)
    sock.sendall((f'GET {url.path} HTTP/1.1\r\nHost: {url.netloc}\r\nOrigin: {origin}\r\n'
                  'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n'
                  'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
    header=b''
    while not header.endswith(b'\r\n\r\n'):
        part=sock.recv(1)
        if not part:sock.close();return None
        header+=part
    assert b'101 Switching Protocols' in header
    return sock

def exact(sock,n):
    result=b''
    while len(result)<n:
        part=sock.recv(n-len(result));assert part;result+=part
    return result

def exchange(sock,sequence,offset,payload):
    packet=bytearray(104)+payload;packet[:6]=b'RWR\1\1\1'
    struct.pack_into('<QQI',packet,8,sequence,offset,len(payload));packet[32:96]=b'a'*64
    struct.pack_into('<I',packet,96,zlib.crc32(payload))
    mask=b'\x11\x22\x33\x44';length=len(packet)
    frame=b'\x82'+(bytes([128|length]) if length<126 else b'\xfe'+struct.pack('!H',length) if length<65536 else b'\xff'+struct.pack('!Q',length))
    sock.sendall(frame+mask+bytes(v^mask[i%4] for i,v in enumerate(packet)))
    header=exact(sock,2);n=header[1]&127
    if n==126:n=struct.unpack('!H',exact(sock,2))[0]
    elif n==127:n=struct.unpack('!Q',exact(sock,8))[0]
    response=exact(sock,n);assert response[:5]==b'RWR\1\1'
    assert response[32:96]==b'a'*64
    return response

process=subprocess.Popen(sys.argv[1:],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
try:
    info=json.loads(process.stdout.readline());assert info['mode']=='jsfxResourceV1'
    assert connect(info,'http://wrong.invalid') is None
    sock=connect(info,'http://jsfx-probe.test');assert sock
    first=bytes(range(256))*1024;last=b'\x00\xffHELLO'
    ack=exchange(sock,1,0,first);assert ack[5]==2 and len(ack)==104
    assert exchange(sock,1,0,first)==ack
    assert exchange(sock,1,0,b'bad')[5]==3
    assert exchange(sock,3,len(first),last)[5]==3
    assert exchange(sock,2,len(first),last)[5]==2
    checksum=hashlib.sha256(first+last).hexdigest()
    process.stdin.write(checksum+'\n');process.stdin.flush()
    result=json.loads(process.stdout.readline());assert result=={'bytes':len(first+last),'sha256':checksum}
    sock.close();assert process.wait(timeout=5)==0
    print('JSFX reliable transport: protected origin, 256 KiB chunks, retry, ordering and SHA-256 passed')
finally:
    if process.poll() is None:process.terminate();process.wait(timeout=5)
