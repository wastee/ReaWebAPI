"""External control protocol and coexistence, over real loopback sockets."""
import json
import base64
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlsplit


def exact(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise EOFError('Connection closed')
        data += part
    return bytes(data)


class WebSocket:
    def __init__(self, url, origin='null'):
        address = urlsplit(url)
        self.sock = socket.create_connection((address.hostname, address.port), timeout=3)
        self.sock.sendall((f'GET {address.path or "/"} HTTP/1.1\r\nHost: {address.netloc}\r\n'
                           f'Origin: {origin}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                           'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
        header = b''
        while not header.endswith(b'\r\n\r\n'):
            header += exact(self.sock, 1)
        assert b'101 Switching Protocols' in header and b's3pPLMBiTxaQ9kYGzzhZRbK+xOo=' in header
        self.sequence = 0
        self.events = []

    def frame(self, payload, opcode=1, final=True):
        if isinstance(payload, str):
            payload = payload.encode()
        first = (128 if final else 0) | opcode
        size = len(payload)
        header = bytes([first, 128 | size]) if size < 126 else (
            bytes([first, 254]) + struct.pack('!H', size) if size <= 65535 else bytes([first, 255]) + struct.pack('!Q', size))
        mask = os.urandom(4)
        self.sock.sendall(header + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload)))

    def receive_frame(self):
        header = exact(self.sock, 2)
        size = header[1] & 127
        if size == 126:
            size = struct.unpack('!H', exact(self.sock, 2))[0]
        elif size == 127:
            size = struct.unpack('!Q', exact(self.sock, 8))[0]
        assert header[0] & 128 and not header[1] & 128
        return header[0] & 15, exact(self.sock, size)

    def receive(self):
        opcode, payload = self.receive_frame()
        if opcode == 8:
            raise EOFError('WebSocket close')
        assert opcode == 1, opcode
        return json.loads(payload)

    def request(self, method, params=None, fragmented=False):
        self.sequence += 1
        message = json.dumps(dict(type='request', id=self.sequence, method=method, params=params or {}))
        if fragmented:
            self.frame(message[:20], final=False)
            self.frame(message[20:], opcode=0)
        else:
            self.frame(message)
        return self.sequence

    def response(self, request):
        while True:
            message = self.receive()
            if message['type'] == 'event':
                self.events.append(message)
                continue
            assert message['type'] == 'response' and message['id'] == request, message
            return message

    def call(self, method, params=None, error=None, fragmented=False):
        result = self.response(self.request(method, params, fragmented))
        if error:
            assert result.get('error', {}).get('code') == error, result
            return result['error']
        assert 'error' not in result, result
        return result['result']

    def closed(self):
        try:
            while True:
                self.receive()
        except (EOFError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            self.sock.close()

    def close(self):
        self.sock.close()


def main():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='reaweb-external-') as directory:
        process = subprocess.Popen([sys.argv[1], str(port), directory], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8')
        clients = []
        try:
            config = json.loads(process.stdout.readline())
            url = f'ws://127.0.0.1:{config["port"]}'

            def command(cmd, expect=None, **params):
                process.stdin.write(json.dumps(dict(cmd=cmd, **params)) + '\n')
                process.stdin.flush()
                result = json.loads(process.stdout.readline())
                if expect:
                    assert result.get('error') == expect, result
                    return result
                assert 'error' not in result, result
                return result['result']

            def connect(token=None):
                client = WebSocket(url)
                clients.append(client)
                if token:
                    assert client.call('auth.authenticate', dict(token=token, protocolVersion=1)) == dict(authenticated=True, protocolVersion=1)
                return client

            a = connect()
            a.call('api.call', dict(name='CountTracks', args=[0]), error='AUTH_REQUIRED')
            a.call('auth.authenticate', dict(token='wrong', protocolVersion=1), error='AUTH_FAILED')
            a.closed()
            wrong_version = connect()
            wrong_version.call('auth.authenticate', dict(token=config['token'], protocolVersion=2), error='PROTOCOL_MISMATCH')
            wrong_version.closed()
            a, b = connect(config['token']), connect(config['token'])
            assert command('stats')['platforms'] == 0, 'External Client must not create a WebView'
            assert a.call('system.getInfo')['protocolVersion'] == 1
            capabilities = a.call('system.getCapabilities')
            assert 'CountTracks' in capabilities['api']['availableMethods']
            assert len(capabilities['events']) == 9
            a.call('system.getInfo', {'extra': True}, error='INVALID_ARGUMENT')
            a.call('window.close', error='UNKNOWN_METHOD')
            with socket.socket() as occupied:
                occupied.bind(('127.0.0.1', 0)); occupied.listen()
                command('port', value=occupied.getsockname()[1], expect='TRANSPORT_UNAVAILABLE')
                assert a.call('system.getInfo')['protocolVersion'] == 1
            assert a.call('api.call', dict(name='CountTracks', args=[0]), fragmented=True) == 1
            a.frame('ping', opcode=9)
            assert a.receive_frame() == (10, b'ping')
            for forbidden in ['ReaWeb_Open', 'ReaWeb_Batch', 'GetFunc', 'reaper.window.close', 'BR_GetMouseCursorContext']:
                a.call('api.call', dict(name=forbidden, args=[]), error='UNKNOWN_API')
            a.call('api.call', dict(name='CountTracks', args=[]), error='INVALID_ARGUMENT')
            a.call('api.call', dict(name='InsertTrackAtIndex', args=[0, False]), error='API_UNAVAILABLE')
            handle = a.call('api.call', dict(name='GetTrack', args=[0, 0]))
            assert handle['type'] == 'MediaTrack' and len(handle['id']) > 64
            assert a.call('api.call', dict(name='GetTrackName', args=[handle])) == [True, 'Track 测试']
            b.call('api.call', dict(name='GetTrackName', args=[handle]), error='INVALID_HANDLE')
            a.call('api.call', dict(name='GetTrackName', args=[dict(type='MediaTrack', id='forged')]), error='INVALID_HANDLE')
            batch = [dict(method='GetTrack', args=[0, 0]), dict(method='GetTrackName', args=[{'$ref': 0}])]
            assert a.call('api.batch', dict(calls=batch, undoLabel='External test'))[1] == [True, 'Track 测试']
            assert command('stats')['undo'] == command('stats')['refresh'] == 0
            failing = [dict(method='CountTracks', args=[0]), dict(method='SetMediaTrackInfo_Value', args=[handle, 'TEST', 9])]
            error = a.call('api.batch', dict(calls=failing), error='BATCH_FAILED')
            assert error['details']['completed'] == 1 and error['details']['rolledBack'] is False
            a.call('api.batch', dict(calls=[dict(method='Main_OnCommand', args=[0, 0])]), error='INVALID_ARGUMENT')
            a.call('api.batch', dict(calls=[dict(method='CountTracks', args=[0])] * 129), error='INVALID_ARGUMENT')
            item = a.call('api.call', dict(name='GetMediaItem', args=[0, 0]))
            take = a.call('api.call', dict(name='GetActiveTake', args=[item]))
            encoded = {'__reawebBytes': base64.b64encode(bytes(range(256))).decode()}
            assert a.call('api.call', dict(name='MIDI_SetAllEvts', args=[take, encoded])) is True
            assert a.call('api.call', dict(name='MIDI_GetAllEvts', args=[take])) == [True, encoded]
            accessor = a.call('api.call', dict(name='CreateTrackAudioAccessor', args=[handle]))
            doubles = {'__reawebFloat64': base64.b64encode(struct.pack('<dd', 0.5, -1.0)).decode()}
            writeback = a.call('api.call', dict(name='GetAudioAccessorSamples', args=[accessor, 48000, 1, 0, 2, doubles]))
            assert writeback['__reawebCall'] is True and writeback['value'] == 1
            assert writeback['arrays'][0]['index'] == 5
            assert struct.unpack('<dd', base64.b64decode(writeback['arrays'][0]['values']['__reawebBytes'])) == (0.75, -0.75)
            payload = {'unicode': '外部客户端', 'buffer': {'type': 'binary', 'data': 'AAH/gA=='}}
            assert a.call('service.invoke', dict(service='test', method='echo', payload=payload)) == payload
            assert a.call('service.invoke', dict(service='test', method='consumer')) == {'windowId': 0}
            assert a.call('service.send', dict(service='test', method='input', payload=payload)) == {'accepted': True}
            a.call('service.invoke', dict(service='absent', method='echo'), error='SERVICE_NOT_FOUND')
            a.call('service.invoke', dict(service='test', method='absent'), error='METHOD_NOT_FOUND')
            assert command('legacy')['result'] == 1
            assert command('legacy', method='GetTrackName', args=[handle])['error']['code'] == 'STALE_HANDLE'
            undo = command('legacy', method='ReaWeb_BeginUndo', args=['Legacy undo'])['result']
            a.call('api.call', dict(name='CountTracks', args=[0]), error='UNDO_BUSY')
            command('legacy', method='ReaWeb_EndUndo', args=[undo])
            assert a.call('api.call', dict(name='CountTracks', args=[0])) == 1
            subscription = a.call('service.subscribe', dict(service='test', event='update'))['subscriptionId']
            command('emit', target=True)
            a.call('system.getInfo')
            assert not any(e.get('subscriptionId') == subscription for e in a.events), 'Window-targeted events leaked'
            command('emit')
            a.call('system.getInfo')
            assert any(e.get('subscriptionId') == subscription and e.get('service') == 'test' for e in a.events)
            native = a.call('events.subscribe', dict(event='projectChanged'))['subscriptionId']
            deadline = time.monotonic() + 2
            while not any(e.get('subscriptionId') == native for e in a.events) and time.monotonic() < deadline:
                time.sleep(0.02)
                a.call('system.getInfo')
            event = next(e for e in a.events if e.get('subscriptionId') == native)
            assert event['data']['revision'] >= 1 and event['data']['projectEpoch'] >= 1
            assert len(event['data']['activeProject']) > 64 and ':' in event['data']['activeProject']
            assert a.call('events.unsubscribe', dict(subscriptionId=native)) is True
            b.call('service.unsubscribe', dict(subscriptionId=subscription), error='INVALID_ARGUMENT')
            stream = a.call('stream.open', dict(name='test.binary'))
            stream_socket = WebSocket(stream['endpoint'], 'http://arbitrary.external.origin')
            clients.append(stream_socket)
            try:
                WebSocket(stream['endpoint'])
                raise AssertionError('Stream ticket was reused')
            except (EOFError, ConnectionResetError):
                pass
            command('publish')
            opcode, packet = stream_socket.receive_frame()
            assert opcode == 2 and packet[:4] == b'RWS\x01' and packet[40:] == b'\0\xff\x80\1'
            stream_socket.frame(b'\1', opcode=2)
            b.call('stream.close', dict(consumerId=stream['consumerId']), error='INVALID_ARGUMENT')
            assert a.call('stream.close', dict(consumerId=stream['consumerId'])) is True
            stream_socket.closed()
            # A pending invoke may finish after a later synchronous request.
            pending_id = a.request('service.invoke', dict(service='test', method='pending'))
            info_id = a.request('system.getInfo')
            assert a.response(info_id)['result']['protocolVersion'] == 1
            command('complete')
            assert a.response(pending_id)['result'] == 42
            overflow = connect(config['token'])
            for _ in range(capabilities['limits']['pendingRequests'] + 1):
                overflow.request('service.invoke', dict(service='test', method='pending'))
            overflow.closed()
            duplicate = connect(config['token'])
            pending_id = duplicate.request('service.invoke', dict(service='test', method='pending'))
            duplicate.call('system.getInfo')
            duplicate.frame(json.dumps(dict(type='request', id=pending_id, method='system.getInfo', params={})))
            duplicate.closed()
            # Disconnect cancels invokes and removes only this client's consumers.
            a.call('stream.open', dict(name='test.binary'))
            b.call('stream.open', dict(name='test.binary'))
            a.request('service.invoke', dict(service='test', method='pending'))
            a.call('system.getInfo')
            a.close()
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                stats = command('stats')
                if stats['cancelled'] >= 2 and stats['streams']['consumers'] == 1:
                    break
                time.sleep(0.01)
            assert stats['cancelled'] >= 2 and stats['streams']['consumers'] == 1, stats
            assert stats['accessorsDestroyed'] == 1, stats
            assert command('legacy')['result'] == 1
            token = command('rotate')['token']
            b.closed()
            assert command('stats')['streams']['consumers'] == 0
            revoked = connect()
            revoked.call('auth.authenticate', dict(token=config['token'], protocolVersion=1), error='AUTH_FAILED')
            revoked.closed()
            c = connect(token)
            c.call('api.call', dict(name='GetTrackName', args=[handle]), error='INVALID_HANDLE')
            live_handle = c.call('api.call', dict(name='GetTrack', args=[0, 0]))
            command('invalidate')
            c.call('api.call', dict(name='GetTrackName', args=[live_handle]), error='STALE_HANDLE')
            c.call('service.subscribe', dict(service='test', event='update'))
            c.request('service.invoke', dict(service='test', method='pending'))
            c.call('system.getInfo')
            command('unload')
            messages = [c.receive(), c.receive()]
            assert any(m.get('error', {}).get('code') == 'EXTENSION_UNLOADED' for m in messages)
            assert any(m.get('event') == 'unloaded' for m in messages)
            command('enabled', value=False)
            c.closed()
            assert command('legacy')['result'] == 1
            command('enabled', value=True)
            d = connect(token)
            assert d.call('api.call', dict(name='CountTracks', args=[0])) == 1
            command('stop')
            assert process.wait(timeout=5) == 0
            assert 'WebViewColorProfile=sRGB' in Path(directory, 'ReaWebAPI.ini').read_text()
            print('External protocol, native reuse, ownership, authentication, binary stream and WebView coexistence passed')
        finally:
            for client in clients:
                client.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    main()
