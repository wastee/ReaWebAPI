"""Run the opt-in macOS keyboard module in isolated REAPER profiles."""
import argparse,json,shutil,subprocess,time
from pathlib import Path
from keyboard_reaper import PAGE
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--extension',type=Path,required=True)
p.add_argument('--probe',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline',action='store_true')
p.add_argument('--controls-only',action='store_true')
p.add_argument('--capture-disabled',action='store_true')
p.add_argument('--reference',type=Path)
p.add_argument('--jsfx',choices=['default','true','false'])
p.add_argument('--chain',action='store_true')
a=p.parse_args()
if a.jsfx=='false' and not (a.baseline or a.reference):
    p.error('--jsfx false requires --reference <baseline-result.json> or --baseline')
session=subprocess.check_output(['ioreg','-n','Root','-d1'],text=True)
if '"CGSSessionScreenIsLocked"=Yes' in session:
    raise SystemExit('Unlock the macOS desktop before native keyboard acceptance.')
root=a.output.resolve();root.mkdir(parents=True,exist_ok=False)
(root/'UserPlugins').mkdir();(root/'keyboard-test.enabled').touch()
for source in [a.extension,a.probe]:
    dest=root/'UserPlugins'/source.name;shutil.copy2(source,dest)
    subprocess.run(['codesign','--force','--sign','-',str(dest)],check=True,capture_output=True)
for name in ['reaper-license.rk','reaper-install-rev.txt']:
    source=Path.home()/'Library/Application Support/REAPER'/name
    if source.exists():shutil.copy2(source,root/name)
for source in (Path.home()/'Library/Application Support/REAPER').glob('reaper-*plugins*.ini'):
    shutil.copy2(source,root/source.name)
(root/'EmptyVST').mkdir()
(root/'reaper.ini').write_text('[reaper]\nmixwnd_vis=0\nverchk=0\nerrnowarn=5\nloadlastproj=0\nshowlastproj=0\nsplash=0\nhasrecentlyopened=1\nnewprojdo=0\nvst_scan=0\nvstfullstate=49989\nvstpath='+str(root/'EmptyVST')+'\nvstpath64='+str(root/'EmptyVST')+'\n[audioconfig]\nmode=4\n')
# Custom binding to Stop, observed by hookcommand2 (not Play/Stop).
(root/'reaper-kb.ini').write_text('KEY 1 32 1016 0\n')
folder=root/('Effects/keyboard' if a.jsfx else 'Scripts/Keyboard');folder.mkdir(parents=True)
(folder/'index.html').write_text(PAGE)
if not a.jsfx:(folder/'app.json').write_text('{"id":"keyboard-acceptance"}')
if a.jsfx:
    (folder/'probe.jsfx').write_text('desc:Keyboard acceptance\nslider1:0.5<0,1,0.01>Gain\n@sample\nspl0*=slider1;spl1*=slider1;\n')
    ui=dict(entry='index.html',width=640,height=420)
    if a.jsfx!='default':ui['dawKeyboard']=a.jsfx=='true'
    (folder/'probe.json').write_text(json.dumps(dict(schema=1,JSFX_WebUI=ui)))
    shutil.copy2(Path(__file__).resolve().parents[1]/'runtime/jsfx-webui.mjs',folder/'jsfx-webui.mjs')
    with (folder/'index.html').open('a') as file:file.write('<script type="module">import {connectJSFX} from "./jsfx-webui.mjs";window.jsfx=await connectJSFX(reaper);</script>')
def wait(fn,timeout=20):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if process.poll() is not None: raise RuntimeError("REAPER exited: "+str(process.returncode))
        value=fn()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Timed out')
def cmd(op,**args):
    (root/'keyboard-reply.json').unlink(missing_ok=True)
    (root/'keyboard-command.tmp').write_text(json.dumps(dict(op=op,**args)))
    (root/'keyboard-command.tmp').replace(root/'keyboard-command.json')
    wait(lambda:(root/'keyboard-reply.json').exists())
    value=json.loads((root/'keyboard-reply.json').read_text())
    assert not isinstance(value,dict) or 'error' not in value,value
    return value
def ev(script):return cmd('eval',script=script)
def press(key,flags=0):
    cmd('key',key=key,down=True,flags=flags);time.sleep(.12)
    cmd('key',key=key,down=False,flags=flags);time.sleep(.2)
report=[]
with (root/'process.log').open('w') as log:
    process=subprocess.Popen(['/Applications/REAPER.app/Contents/MacOS/REAPER','-newinst','-cfgfile',str(root/'reaper.ini'),'-new','-ignoreerrors','-nosplash','-ApplePersistenceIgnoreState','YES'],stdout=log,stderr=log)
    try:
        wait(lambda:(root/'keyboard-ready').exists(),120)
        if a.jsfx:cmd('jsfx',floating=not a.chain)
        else:cmd('open')
        wait(lambda:cmd('select'));time.sleep(1)
        if a.jsfx:wait(lambda:ev('!!window.jsfx'))
        cmd('focus',content=True);time.sleep(.5)
        assert cmd('state')['keyWindow'] and ev('document.hasFocus()'), 'The test WebView must be active'
        if a.jsfx:
            for target in ["document.querySelector('#slider')","document.querySelector('#button')","document.querySelector('#text')","document.querySelector('#area')","document.querySelector('#edit')",'openInput','closedInput']:
                ev(f'{target}.focus();keys=[];true');before=cmd('state')['shortcuts'];press(49)
                state=cmd('state');keys=ev('keys');route=a.jsfx!='false' and target in ["document.querySelector('#slider')","document.querySelector('#button')"]
                result=dict(target=target,shortcuts=state['shortcuts']-before,command=state['command'],keys=keys,focused=ev('document.hasFocus()'))
                report.append(result)
                if a.baseline or a.reference:continue
                assert result['shortcuts']==int(route) and (not route or state['command']==1016),result
                assert len(keys)==(0 if route else 2),result
            if a.jsfx!='false' and not a.baseline:
                ev("document.querySelector('#slider').focus();keys=[];true")
                before=cmd('state')['shortcuts']
                cmd('key',key=49,down=True);time.sleep(.2)
                first=cmd('state')['shortcuts']
                cmd('key',key=49,down=True,repeat=True);time.sleep(.2)
                repeated=cmd('state')['shortcuts']
                ev("document.querySelector('#text').focus();true")
                cmd('key',key=49,down=True,repeat=True,flags=1<<17);time.sleep(.2)
                modified_repeat=cmd('state')['shortcuts']
                cmd('key',key=49,down=False,flags=1<<17);time.sleep(.2)
                result=dict(heldSpace=True,shortcuts=cmd('state')['shortcuts']-before,repeatDeltas=[repeated-first,modified_repeat-repeated],keys=ev('keys'))
                report.append(result);assert result['shortcuts']>=1 and not result['keys'],result
                assert result['repeatDeltas'][0]==result['repeatDeltas'][1], result
                ev("document.querySelector('#slider').focus();keys=[];true")
                before=cmd('state')['shortcuts'];press(49,1<<17)
                assert cmd('state')['shortcuts']==before, 'A new modified Space inherited the previous press'
                ev('keys=[];true');cmd('key',key=49,down=True);time.sleep(.2)
                before=cmd('state')['shortcuts']
                cmd('focus',main=True);time.sleep(.3);cmd('focus');time.sleep(.3)
                cmd('key',key=49,down=True,repeat=True);time.sleep(.2)
                cmd('key',key=49,down=False);time.sleep(.2)
                result=dict(blurWhileHeld=True,shortcuts=cmd('state')['shortcuts']-before,keys=ev('keys'))
                report.append(result);assert result['shortcuts']==0 and not result['keys'],result
        else:
            capture='false' if a.capture_disabled else 'true'
            ev('window.captureReady=false;reaper.window.setKeyboardCapture('+capture+').then(()=>captureReady=true);true');wait(lambda:ev('captureReady'))
            if a.controls_only:
                for docked in [False,True]:
                    if docked:cmd('dock',value=True);cmd('focus',content=True);time.sleep(.5)
                    for key,flags in ([(36,0)] if a.capture_disabled else [])+[(36,1<<17),(36,1<<20),(123,0),(49,0),(0,0)]:
                        ev('keys=[];true');press(key,flags)
                        report.append(dict(docked=docked,key=key,flags=flags,keys=ev('keys'),focused=ev('document.hasFocus()')))
            for docked in [False,True]:
                if a.controls_only:break
                if docked:cmd('dock',value=True);cmd('focus',content=True);time.sleep(.5)
                ev('keys=[];true')
                for _ in range(3):press(36)
                result=dict(docked=docked,keys=ev('keys'),focused=ev('document.hasFocus()'),native=cmd('state'));report.append(result)
                if a.baseline:
                    assert not result['focused'] and len(result['keys'])<6,result
                    break
                assert len(result['keys'])==6 and result['focused'],result
                if not docked:
                    cmd('focus',main=True);time.sleep(.3);cmd('focus');time.sleep(.3);ev('keys=[];true');press(36)
                    result=dict(reactivation=True,keys=ev('keys'),focused=ev('document.hasFocus()'));report.append(result)
                    assert len(result['keys'])==2 and result['focused'],result
                for key,flags in [(36,1<<17),(36,1<<20),(123,0),(49,0),(0,0)]:
                    ev('keys=[];true');press(key,flags)
                    result=dict(key=key,flags=flags,keys=ev('keys'),focused=ev('document.hasFocus()'));report.append(result)
                    # Compare container-specific shortcut delivery using --controls-only and --reference.
                    assert result['focused'],result
        if a.reference:
            assert report==json.loads(a.reference.read_text()), 'Native behavior differs from the reference run'
    finally:
        (root/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
        process.terminate();process.wait(timeout=10)
        (root/'reaper-license.rk').unlink(missing_ok=True)
