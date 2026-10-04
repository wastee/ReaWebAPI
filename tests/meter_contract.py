"""Built-in meter history through the unchanged ABI 1 binary transport."""
import json
import math
import socket
import struct
import subprocess
import sys
import time
from urllib.parse import urlsplit


process = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)


def command(command, **args):
    process.stdin.write(json.dumps(dict(command=command, **args)) + '\n')
    process.stdin.flush()
    response = json.loads(process.stdout.readline())
    assert 'result' in response, response
    return response['result']


def exact(sock, count):
    result = b''
    while len(result) < count:
        data = sock.recv(count - len(result))
        assert data, 'stream closed'
        result += data
    return result


def open_meter(**options):
    info = command('open', options=dict(updateRate=120, **options))
    address = urlsplit(info['url'])
    sock = socket.create_connection((address.hostname, address.port), timeout=3)
    sock.sendall((f'GET {address.path} HTTP/1.1\r\nHost: {address.netloc}\r\n'
                  'Origin: http://127.0.0.1:9000\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                  'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
    header = b''
    while not header.endswith(b'\r\n\r\n'):
        header += exact(sock, 1)
    assert b'101 Switching Protocols' in header
    return info['name'], sock


def meter(sock, predicate=lambda m: True):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        header = exact(sock, 2)
        assert header[0] & 15 == 2, header
        count = header[1] & 127
        if count == 126:
            count = struct.unpack('!H', exact(sock, 2))[0]
        data = exact(sock, count)
        assert data[:5] == b'RWS\x01\x04' and (len(data) - 40) % 4 == 0
        values = struct.unpack_from('<' + 'f' * ((len(data) - 40) // 4), data, 40)
        sock.sendall(b'\x82\x81\x11\x22\x33\x44\x10')
        if predicate(values):
            return values
    raise AssertionError('meter predicate timed out')


def await_closed():
    deadline = time.monotonic() + 3
    while command('info')['streams']:
        assert time.monotonic() < deadline, 'stream did not close'
        command('tick')
        time.sleep(.01)


def empty_history(values):
    assert all(value == 0 for value in values[:14]), values
    assert values[20] == values[23] == values[24] == values[25] == 0, values
    assert values[26] == values[27] == -100, values
    assert all(values[i] == -math.inf for i in (14, 15, 16, 17, 18, 19, 21, 22)), values


def restarted_history(sock):
    values = meter(sock, lambda m: abs(m[25] - .1) < 1e-5)
    assert abs(values[0] - .1) < 1e-5 and abs(values[23] - .1) < 1e-5, values
    assert .09 < values[24] < .12, values
    assert all(values[i] == -math.inf for i in (14, 15, 16, 17, 18, 19, 21, 22)), values


try:
    for aggregate in (False, True):
        name, sock = open_meter(source='selected-track', aggregate=aggregate)
        assert meter(sock)[25] == 0
        before = command('tick', position=0, playing=0)['reads']
        for _ in range(4):
            assert command('tick')['reads'] == before
        command('tick', playing=1)
        for i in range(1, 51):
            command('tick', position=i / 50)
        data = meter(sock, lambda m: m[25] >= .99)
        assert abs(data[25] - 1) < 1e-5 and abs(data[15] - 10 * math.log10(.25 * .85)) < .001, data
        assert command('tick', position=1, playing=0)['reads'] == before + 50
        for _ in range(4):
            assert command('tick')['reads'] == before + 50
        joined_name, joined = open_meter(source='selected-track', aggregate=aggregate)
        command('tick')
        assert meter(joined)[25] == 0
        command('reset', name=name)
        reset = meter(sock, lambda m: m[25] == 0)
        empty_history(reset)
        command('tick', playing=1)
        for i in range(1, 21):
            command('tick', position=1 + i / 50)
        meter(sock, lambda m: m[25] >= .39)
        command('tick', position=7)
        empty_history(meter(sock, lambda m: m[25] == 0))
        for i in range(1, 11):
            command('tick', position=7 + i / 50)
        meter(sock, lambda m: m[25] >= .19)
        command('tick', **({'revision': 1} if aggregate else {'refreshed': True}))
        empty_history(meter(sock, lambda m: m[25] == 0))
        command('close'); sock.close(); joined.close()
        command('tick', position=0, playing=0)
    name, sock = open_meter(source='input')
    command('tick', playing=1)
    for _ in range(10):
        command('capture', output=False)
    data = meter(sock, lambda m: m[25] >= .99)
    assert abs(data[15] - 10 * math.log10(.25 * .85)) < .001
    command('reset', name=name)
    empty_history(meter(sock, lambda m: m[25] == 0))
    command('capture', output=False, amplitude=.1)
    restarted_history(sock)
    command('capture', output=False)
    meter(sock, lambda m: m[25] >= .19)
    command('capture', output=False, frames=0)
    command('capture', output=False, amplitude=.1)
    restarted_history(sock)
    command('capture', output=False)
    meter(sock, lambda m: m[25] >= .19)
    command('capture', output=False, channels=1, amplitude=.1)
    command('tick')
    await_closed()
    sock.close()
    command('configure', inputChannels=2)
    name, sock = open_meter(source='input')
    command('capture', output=False, amplitude=.1)
    restarted_history(sock)
    command('capture', output=False)
    meter(sock, lambda m: m[25] >= .19)
    time.sleep(.45)
    command('capture', output=False, amplitude=.1)
    restarted_history(sock)
    command('tick', playing=0)
    command('tick', playing=1)
    empty_history(meter(sock, lambda m: m[25] == 0))
    command('capture', output=False, amplitude=.1)
    restarted_history(sock)
    command('close'); sock.close()
    name, sock = open_meter(source='master', forceMono=True, resetOnPlaybackStart=False)
    for _ in range(5):
        command('capture')
    data = meter(sock, lambda m: m[25] >= .49)
    assert abs(data[15] - (10 * math.log10(.25 * .7) - 3)) < .001
    command('tick', playing=0)
    for _ in range(5):
        command('capture', amplitude=1.5)
    stopped = meter(sock, lambda m: m[25] >= .99)
    assert stopped[15] == data[15] and stopped[19] == data[19], stopped
    assert stopped[6] > 0 and stopped[8] > 0 and stopped[10] == stopped[23] == 1.5, stopped
    assert stopped[12] == stopped[24] and stopped[12] >= 1.5, stopped
    command('tick', playing=1); command('capture')
    resumed = meter(sock, lambda m: m[25] >= 1.09)
    assert resumed[15] > stopped[15], resumed
    command('capture', rate=44100); command('tick')
    await_closed()
    sock.close()
    name, sock = open_meter(source='master')
    command('capture', frames=4410)
    meter(sock, lambda m: abs(m[25] - .1) < 1e-5)
    command('close'); sock.close()
    command('configure', inputChannels=2, outputChannels=2, trackChannels=2)
    # Both strategies consume the same hardware packets. Only histories may diverge.
    command('tick', playing=0)
    continuous_name, continuous = open_meter(source='input', integratedMode='continuous', resetOnPlaybackStart=False)
    playback_name, playback = open_meter(source='input', resetOnPlaybackStart=False)
    for _ in range(60):
        command('capture', output=False, frames=4410)
    c = meter(continuous, lambda m: m[25] >= 5.99)
    p = meter(playback, lambda m: m[25] >= 5.99)
    assert math.isfinite(c[15]) and math.isfinite(c[19]) and c[26] > -100, c
    assert p[15] == p[19] == -math.inf and p[26] == p[27] == -100, p
    for i in (14, 17, 18, 21, 22, 23, 24, 25):
        assert c[i] == p[i], (i, c, p)
    command('tick', playing=1)
    for _ in range(60):
        command('capture', output=False, frames=4410, amplitude=1.5)
    c = meter(continuous, lambda m: m[25] >= 11.99)
    p = meter(playback, lambda m: m[25] >= 11.99)
    assert p[26] > -100 and math.isfinite(p[19]) and p[15] > c[15], (p, c)
    command('tick', playing=0)
    for _ in range(60):
        command('capture', output=False, frames=4410, amplitude=.1)
    frozen = meter(playback, lambda m: m[25] >= 17.99)
    running = meter(continuous, lambda m: m[25] >= 17.99)
    for i in (15, 19, 20, 26, 27):
        assert frozen[i] == p[i], (i, frozen, p)
    assert running[15] != c[15]
    command('reset', name=continuous_name)
    empty_history(meter(continuous, lambda m: m[25] == 0))
    command('close'); continuous.close(); playback.close()
    for channels in (1, 2, 6, 32):
        for source, aggregate in (('master', False), ('input', False), ('selected-track', False), ('selected-track', True)):
            command('configure', trackChannels=channels, inputChannels=channels, outputChannels=channels)
            command('tick', playing=0, position=0)
            name, sock = open_meter(source=source, aggregate=aggregate, integratedMode='continuous')
            previous_reads = command('tick', playing=1)['reads']
            if source == 'selected-track':
                deadline = time.monotonic() + 2
                while command('tick', position=.1)['reads'] == previous_reads:
                    assert time.monotonic() < deadline, (source, channels, 'PCM tick budget')
            else:
                command('capture', output=source == 'master', frames=4410, amplitude=.02, channelRamp=True)
            base = 7 * channels
            try:
                m = meter(sock, lambda m: m[base + 11] >= .099)
            except Exception as error:
                raise AssertionError((source, aggregate, channels)) from error
            assert len(m) == 13 * channels + 14, (channels, m)
            assert abs(m[channels-1] - (.5 if source == 'selected-track' else .02*channels)) < .001, (source, channels, m)
            assert m[base+9] == max(m[5*channels:6*channels]) and m[base+10] == max(m[6*channels:7*channels]), m
            assert all(v == 0 for v in m[base+14:]), m
            command('close'); sock.close()
    # Missing hook channel counts must use the same device fallback as creation.
    for channels in (1, 2, 6, 16, 32, 40):
        count = min(channels, 32)
        for source in ('master', 'input'):
            command('configure', inputChannels=channels, outputChannels=channels, callbackChannels=0)
            name, sock = open_meter(source=source, integratedMode='continuous')
            for _ in range(5):
                command('capture', output=source == 'master', callbackChannels=0, frames=4410, amplitude=.02, channelRamp=True)
                command('tick')
            m = meter(sock, lambda m: m[7*count+11] >= .49)
            assert len(m) == 13*count+14, (source, channels, len(m))
            for ch in range(count):
                assert abs(m[ch] - .02*(ch+1)) < .001, (source, channels, ch, m[ch])
            assert math.isfinite(m[7*count]), (source, channels, m)
            command('close'); sock.close()
    # Device fallback is per direction. A positive hook count takes precedence.
    for count, source, hook in ((16, 'master', 0), (2, 'input', 0), (6, 'master', 6), (1, 'input', 1)):
        command('configure', inputChannels=2, outputChannels=16, callbackChannels=hook)
        name, sock = open_meter(source=source)
        command('capture', output=source == 'master', callbackChannels=hook, frames=4410, amplitude=.02, channelRamp=True)
        m = meter(sock, lambda m: m[7*count+11] >= .099)
        assert len(m) == 13*count+14, (source, count, len(m))
        for ch in range(count):
            assert abs(m[ch] - .02*(ch+1)) < .001, (source, ch, m[ch])
        command('close'); sock.close()
    # Preserve the first-buffer guard and the legacy missing-right fallback.
    for count, source in ((16, 'master'), (2, 'input')):
        command('configure', inputChannels=2, outputChannels=16, callbackChannels=0)
        name, sock = open_meter(source=source)
        for mask in (0, 2):
            command('capture', output=source == 'master', callbackChannels=0, bufferMask=mask, frames=4410)
        command('capture', output=source == 'master', callbackChannels=0, bufferMask=1, frames=4410, amplitude=.02, channelRamp=True)
        m = meter(sock, lambda m: m[7*count+11] >= .099)
        assert abs(m[7*count+11] - .1) < 1e-5, m
        for ch in range(count):
            expected = .02 if ch < 2 else 0
            assert abs(m[ch] - expected) < .001, (source, ch, m[ch])
        command('close'); sock.close()
    command('configure', outputChannels=16, callbackChannels=0)
    name, sock = open_meter(source='master')
    command('capture', callbackChannels=0, frames=4410)
    meter(sock, lambda m: m[7*16+11] >= .099)
    command('tick', outputChannels=2)
    command('capture', callbackChannels=0, frames=4410)
    await_closed(); sock.close()
    name, sock = open_meter(source='master')
    command('capture', callbackChannels=0, frames=4410)
    assert len(meter(sock, lambda m: m[25] >= .099)) == 40
    command('close'); sock.close()
    # Channel changes require reopening, keeping immutable metadata honest.
    for aggregate in (False, True):
        command('configure', trackChannels=2)
        name, sock = open_meter(source='selected-track', aggregate=aggregate)
        command('tick')
        command('tick', trackChannels=6)
        await_closed()
        sock.close()
    print('Track/aggregate continuity, stop, seek, reset, reopen, hardware capture and format changes passed')
finally:
    process.stdin.close()
    try:
        assert process.wait(timeout=5) == 0
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
