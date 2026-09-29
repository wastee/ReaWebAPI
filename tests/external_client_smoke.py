"""Opt-in External Client check against an isolated real REAPER process."""
import argparse
import json
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import time

from external_client_contract import WebSocket


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reaper', type=Path, required=True)
    parser.add_argument('--extension', type=Path, required=True)
    parser.add_argument('--stream-extension', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--startup-timeout', type=float, default=45)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / 'UserPlugins').mkdir()
    for source in (args.extension, args.stream_extension):
        target = root / 'UserPlugins' / source.name
        shutil.copy2(source, target)
        if sys.platform == 'darwin':
            subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
    if sys.platform.startswith('linux'):
        for helper in args.extension.parent.glob('reawebapi-webview-*'):
            shutil.copy2(helper, root / 'UserPlugins' / helper.name)
    empty_vst = root / 'EmptyVST'
    empty_vst.mkdir()
    config = ('[REAPER]\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\n'
              f'vst_scan=0\nvstpath={empty_vst}\nvstpath64={empty_vst}\n')
    if sys.platform == 'win32':
        # Keep first-run VST3 path migration from adding system plug-in directories.
        config += 'vstfullstate=49989\n[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\nwaveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_in=0\nwaveout_nch_out=2\n'
    if sys.platform == 'darwin':
        config += 'hasrecentlyopened=1\n[audioconfig]\nmode=4\n'
    if sys.platform.startswith('linux'):
        config += 'linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\n'
    (root / 'reaper.ini').write_text(config)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    token = secrets.token_hex(32)
    (root / 'ReaWebAPI.ini').write_text(f'[ExternalClients]\nEnabled=1\nPort={port}\nAccessToken={token}\n')
    (root / 'index.html').write_text('<!doctype html><title>External coexistence test</title><script src="app.js" defer></script>')
    (root / 'app.js').write_text('''(async () => {
      await reaper.lifecycle.ready;
      const initial = await reaper.CountTracks(0);
      const dispose = await reaper.events.on('trackStateChanged', async () => {
        await reaper.fs.writeText('legacy.json', JSON.stringify({count: await reaper.CountTracks(0)}), {overwrite:true});
      });
      await reaper.fs.writeText('legacy-ready.json', JSON.stringify({count:initial}), {overwrite:true});
    })().catch(error => console.error(error));''')
    (root / 'launch.lua').write_text('''local root = debug.getinfo(1, 'S').source:sub(2):match('^(.*[/\\\\])')
local file = assert(io.open(root .. 'launcher.txt', 'w'))
file:write(reaper.GetResourcePath(), '\\n', tostring(reaper.APIExists('ReaWeb_Open')))
file:close()
assert(reaper.ReaWeb_Open(root .. 'index.html') > 0, reaper.ReaWeb_GetLastError())
''')
    clients = []
    with (root / 'process.log').open('w') as log:
        startup = None
        if sys.platform == 'win32':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
        process = subprocess.Popen([str(args.reaper.resolve()), '-newinst', '-cfgfile', str(root / 'reaper.ini'),
                                    '-new', '-nosplash', '-splashlog', str(root / 'startup.log')],
                                   stdout=log, stderr=log, startupinfo=startup)
        (root / 'process.pid').write_text(str(process.pid))
        try:
            deadline = time.monotonic() + args.startup_timeout
            while True:
                try:
                    a = WebSocket(f'ws://127.0.0.1:{port}')
                    clients.append(a)
                    break
                except OSError as error:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f'REAPER did not accept connections, exit code: {process.poll()}. See {root / "startup.log"} and {root / "process.log"}') from error
                    time.sleep(0.1)
            a.call('auth.authenticate', {'token': token, 'protocolVersion': 1})
            info = a.call('system.getInfo')
            assert a.call('service.invoke', {'service': 'runtime', 'method': 'getInfo'})['version'] == info['version']
            launcher = a.call('api.call', {'name': 'AddRemoveReaScript', 'args': [True, 0, str(root / 'launch.lua'), True]})
            assert launcher > 0, 'Could not register the isolated WebView test launcher'
            a.call('api.call', {'name': 'Main_OnCommand', 'args': [launcher, 0]})
            ready = root / 'legacy-ready.json'
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            assert ready.exists(), 'Legacy WebView failed to start'
            initial = json.loads(ready.read_text())['count']
            assert a.call('api.call', {'name': 'CountTracks', 'args': [0]}) == initial
            event = a.call('events.subscribe', {'event': 'trackStateChanged'})['subscriptionId']
            a.call('api.call', {'name': 'InsertTrackAtIndex', 'args': [initial, True]})
            result = a.call('api.batch', {'calls': [{'method': 'GetTrack', 'args': [0, initial]},
                {'method': 'GetTrackName', 'args': [{'$ref': 0}]}]})
            assert result[0]['type'] == 'MediaTrack' and result[1][0] is True
            legacy = root / 'legacy.json'
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                a.call('system.getInfo')
                try:
                    if json.loads(legacy.read_text())['count'] == initial + 1 and any(e.get('subscriptionId') == event for e in a.events):
                        break
                except (OSError, ValueError):
                    pass
                time.sleep(0.05)
            assert json.loads(legacy.read_text())['count'] == initial + 1
            assert any(e.get('subscriptionId') == event for e in a.events)
            a.call('service.invoke', {'service': 'stream.test', 'method': 'start'})
            stream = a.call('stream.open', {'name': 'test.video'})
            binary = WebSocket(stream['endpoint']); clients.append(binary)
            opcode, packet = binary.receive_frame()
            assert opcode == 2 and packet[:4] == b'RWS\x01' and len(packet) == 40 + 240 * 160 * 4
            binary.frame(b'\1', opcode=2)
            a.close()
            try:
                while binary.receive_frame()[0] != 8:
                    pass
            except (EOFError, ConnectionResetError):
                pass
            binary.close()
            b = WebSocket(f'ws://127.0.0.1:{port}'); clients.append(b)
            b.call('auth.authenticate', {'token': token, 'protocolVersion': 1})
            assert b.call('api.call', {'name': 'CountTracks', 'args': [0]}) == initial + 1
            b.call('api.call', {'name': 'GetTrackName', 'args': [result[0]]}, error='INVALID_HANDLE')
            b.call('api.call', {'name': 'DeleteTrack', 'args': [b.call('api.call', {'name': 'GetTrack', 'args': [0, initial]})]})
            b.call('service.invoke', {'service': 'stream.test', 'method': 'stop'})
            report = {'passed': True, 'info': info, 'checks': ['authentication', 'native calls', 'batch references',
                'shared project events', 'legacy WebView coexistence', 'native service', 'binary stream', 'disconnect cleanup', 'handle isolation']}
            (root / 'result.json').write_text(json.dumps(report, indent=2))
            print(json.dumps(report), flush=True)
        finally:
            for client in clients:
                client.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()


if __name__ == '__main__':
    main()
