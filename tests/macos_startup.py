from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os, shutil, subprocess, tempfile, threading, time
root = Path(os.environ.get('STARTUP_ROOT', str(Path(__file__).resolve().parents[1])))
class Page(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/style.css':
            time.sleep(.4)
            body = b'html{background:#182838}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}'
            kind = 'text/css'
        else:
            body = b'<!doctype html><link rel="stylesheet" href="/style.css"><div id="marker"></div>'
            kind = 'text/html'
        self.send_response(200); self.send_header('Content-Type',kind); self.send_header('Cache-Control','no-store'); self.end_headers(); self.wfile.write(body)
    def log_message(self,*args): pass
server = ThreadingHTTPServer(('127.0.0.1',0),Page)
threading.Thread(target=server.serve_forever,daemon=True).start()
for repeat in range(int(os.environ.get('STARTUP_FROM','0')),int(os.environ.get('STARTUP_RUNS','20'))):
    for variant in map(int, os.environ.get('STARTUP_VARIANTS', '0,1,2,3').split(',')):
        profile = Path(tempfile.mkdtemp(prefix=f'startup-{variant}-{repeat}-',dir=root))
        plugins = profile / 'UserPlugins'; plugins.mkdir()
        for source in [Path(os.environ.get('STARTUP_EXTENSION', str(root/'build-arm64/reaper_reawebapi-arm64.dylib'))),root/'build-arm64/tests/reaper_startup_test.dylib']:
            target = plugins/source.name; shutil.copy2(source,target)
            subprocess.run(['codesign','--force','--sign','-',str(target)],check=True,capture_output=True)
        (profile/'startup-test.enabled').write_text(str(variant))
        if os.environ.get('STARTUP_POOLED') == '1': (profile/'startup-test.pooled').touch()
        (profile/'startup-test.delay').write_text(os.environ.get('STARTUP_IDLE_MS', '3000' if os.environ.get('STARTUP_POOLED') == '1' else '0'))
        if 'STARTUP_IDLE_WEBVIEWS' in os.environ:
            count = int(os.environ['STARTUP_IDLE_WEBVIEWS'])
            assert 0 <= count <= 32
            (profile/'ReaWebAPI.ini').write_text(f'[ReaWebAPI]\nIdleWebViews={count}\n')
        (profile/'startup-test.url').write_text(f'http://127.0.0.1:{server.server_port}/')
        (profile/'reaper.ini').write_text('[reaper]\nmixwnd_vis=0\nverchk=0\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nhasrecentlyopened=1\n[audioconfig]\nmode=4\n')
        print('START',repeat,variant,profile,flush=True)
        for launch in range(3):
            with (profile/'process.log').open('a') as log:
                process=subprocess.Popen(['/Applications/REAPER.app/Contents/MacOS/REAPER','-newinst','-cfgfile',str(profile/'reaper.ini'),'-new','-nosplash'],stdout=log,stderr=log)
                try: process.wait(timeout=35)
                except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=10)
            if (profile/'startup-test.log').exists(): break
            print('REAPER did not reach the test module, exit=',process.returncode,'retry=',launch,flush=True)
        result=(profile/'startup-test.log').read_text()
        print(result,flush=True)
        assert 'PASS:' in result,result


