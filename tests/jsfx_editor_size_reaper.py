"""Exercise a 1175 Web UI's scale control in a disposable native REAPER profile."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
for name in ("reaper", "extension", "effect", "js-api", "output"):
    parser.add_argument("--" + name, type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
plugins = root / "UserPlugins"
plugins.mkdir()
for source in (args.extension, args.js_api):
    shutil.copy2(source, plugins / source.name)
    if sys.platform == "darwin":
        subprocess.run(["codesign", "--force", "--sign", "-", str(plugins / source.name)], check=True, capture_output=True)
effect = root / "Effects/size-acceptance"
shutil.copytree(args.effect, effect)
page = effect / ".webui/index.html"
page.write_text(page.read_text(encoding="utf-8") + '\n<script type="module" src="./size-acceptance.js"></script>\n', encoding="utf-8")
(effect / ".webui/size-acceptance.js").write_text("""
await reaper.lifecycle.ready;
const identity=crypto.randomUUID();
await reaper.window.setTitle('SizeAcceptance_'+identity);
setTimeout(()=>reaper.window.focus(),500);
let busy=false,last='';
setInterval(async()=>{
  if(busy||document.hidden||!document.body.classList.contains('native-window'))return;
  busy=true;
  try{
    const command=await reaper.GetExtState('SizeAcceptance','scale');
    if(command&&command!==last){
      last=command;
      const menu=document.getElementById('ui-zoom');
      menu.value=command.split('|')[1];menu.dispatchEvent(new Event('change'));
    }
    const message=document.getElementById('message');
    await reaper.SetExtState('SizeAcceptance','page',JSON.stringify({
      identity,scale:Number(document.getElementById('ui-zoom').value),
      size:await reaper.window.getSize(),viewport:[innerWidth,innerHeight],dpr:devicePixelRatio,
      error:message.hidden?'':message.textContent
    }),false);
  }catch(error){await reaper.SetExtState('SizeAcceptance','page',JSON.stringify({error:error.message}),false);}
  finally{busy=false;}
},80);
""", encoding="utf-8")
(root / "EmptyVST").mkdir()
for name in ("reaper-install-rev.txt", "reaper-license.rk"):
    source = args.reaper.parent / name
    if source.is_file():
        shutil.copy2(source, root / name)
(root / "reaper.ini").write_text(
    "[REAPER]\nloadlastproj=0\nshowlastproj=0\nsplash=0\nverchk=0\nvst_scan=0\nvstfullstate=49989\n"
    f"vstpath64={root / 'EmptyVST'}\n"
    "hasrecentlyopened=1\nerrnowarn=5\n"
    "[audioconfig]\nmode=4\nwaveout_devicein=-1\nwaveout_deviceout=0\n"
    "waveout_srate=48000\nwaveout_bps=16\nwaveout_bs=256\nwaveout_numblocks=4\nwaveout_nch_out=2\n",
    encoding="utf-8")
lua = r'''
local root = ROOT
local function write(name,value)
  local f=assert(io.open(root..'/'..name,'w'));f:write(value);f:close()
end
local track
local ok,err=pcall(function()
  assert(reaper.APIExists('JS_Window_GetRect'))
  reaper.InsertTrackAtIndex(0,false)
  track=reaper.GetTrack(0,0)
  reaper.GetSetMediaTrackInfo_String(track,'P_NAME','JSFX size acceptance',true)
  reaper.SetMediaTrackInfo_Value(track,'B_MAINSEND',0)
  for i=0,1 do assert(reaper.TrackFX_AddByName(track,'size-acceptance/1175 Compressor.jsfx',false,-1)==i) end
  reaper.TrackFX_Show(track,0,1)
end)
if not ok then write('error.txt',err);return end
local phase='startup'
local owner,web
local count=0
local trace=assert(io.open(root..'/trace.csv','a'))
local function tick()
  local f=io.open(root..'/command.txt','r')
  if f then
    local command=f:read('*a');f:close();os.remove(root..'/command.txt')
    local id,op,value=command:match('^(%d+)|([^|]+)|?(.*)$')
    phase=id..'-'..op..'-'..value
    if op=='scale' then reaper.SetExtState('SizeAcceptance','scale',id..'|'..value,false)
    elseif op=='switch' then reaper.TrackFX_Show(track,tonumber(value),1)
    elseif op=='float' then reaper.TrackFX_Show(track,tonumber(value),3)
    elseif op=='chain' then reaper.TrackFX_Show(track,tonumber(value),2);reaper.TrackFX_Show(track,tonumber(value),1)
    elseif op=='quit' then
      trace:close()
      reaper.Main_SaveProjectEx(0,root..'/acceptance.rpp',8)
      reaper.Main_SaveProject(0,false)
      reaper.Main_OnCommand(40004,0)
      return
    end
    write('reply.txt',id)
  end
  count=count+1
  if count%5==0 then
    local data=reaper.GetExtState('SizeAcceptance','page')
    if data~='' then write('page.json',data) end
    local identity=data:match('"identity":"([^"]+)"')
    local _,tops=reaper.JS_Window_ListAllTop()
    for address in tops:gmatch('[^,]+') do
      local top=reaper.JS_Window_HandleFromAddress(address)
      local _,children=reaper.JS_Window_ListAllChild(top)
      for child in children:gmatch('[^,]+') do
        local h=reaper.JS_Window_HandleFromAddress(child)
        if identity and reaper.JS_Window_GetTitle(h)=='SizeAcceptance_'..identity and reaper.JS_Window_IsVisible(h) then
          web=h;owner=h
          for n=1,32 do
            if (reaper.JS_Window_GetLong(owner,'STYLE')&0x40000000)==0 then break end
            local p=reaper.JS_Window_GetParent(owner);if not p then break end;owner=p
          end
        end
      end
    end
  end
  if owner and reaper.JS_Window_IsWindow(owner) and web and reaper.JS_Window_IsWindow(web) then
    local _,l,t,r,b=reaper.JS_Window_GetRect(owner)
    local _,wl,wt,wr,wb=reaper.JS_Window_GetRect(web)
    trace:write(string.format('%s,%d,%d,%d,%d\n',phase,math.abs(r-l),math.abs(b-t),math.abs(wr-wl),math.abs(wb-wt)))
    trace:flush()
  end
  reaper.defer(function() local ok,err=pcall(tick);if not ok then write('error.txt',err) end end)
end
tick()
'''
(root / "launch.lua").write_text(lua.replace("ROOT", json.dumps(root.as_posix())), encoding="utf-8")


def wait(fn, timeout=45):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if (root / "error.txt").exists():
            raise RuntimeError((root / "error.txt").read_text())
        try:
            result = fn()
            if result:
                return result
        except (OSError, ValueError):
            pass
        time.sleep(.06)
    raise TimeoutError("Native acceptance timed out")


sequence = 0
records = []


def command(op, value=""):
    global sequence
    sequence += 1
    temp = root / "command.tmp"
    temp.write_text(f"{sequence}|{op}|{value}", encoding="utf-8")
    temp.replace(root / "command.txt")
    if op != "quit":
        wait(lambda: (root / "reply.txt").read_text() == str(sequence))


def sample(scale):
    def check():
        data = json.loads((root / "page.json").read_text())
        if data.get("error"):
            raise RuntimeError(data["error"])
        units = 1 if sys.platform == "darwin" else data["dpr"]
        expected = [round(1380 * scale * units), int(__import__("math").ceil((30 + 1380 * 258 / 1400 + 34) * scale)) * units]
        return data if data.get("scale") == scale and all(abs(data["size"][k] - v) <= 2 for k, v in zip(("width", "height"), expected)) else None
    data = wait(check)
    records.append({"step": sequence, **data})
    return data


with (root / "process.log").open("w") as log:
    process = subprocess.Popen([str(args.reaper), "-newinst", "-cfgfile", str(root / "reaper.ini"),
                                "-new", "-nosplash", str(root / "launch.lua")], stdout=log, stderr=log)
    try:
        wait(lambda: (root / "page.json").exists(), 180)
        for scale in (.75, .5, 1.5):
            command("scale", scale)
            sample(scale)
            time.sleep(.4)
            # Warm both pages, then trace repeated cached-page switching.
            for index in (1, 0, 1, 0, 1, 0):
                command("switch", index)
                time.sleep(.7)
                sample(scale)
            for index in (0, 1, 0, 1):
                command("float", index)
                time.sleep(.8)
                sample(scale)
                command("chain", index)
                time.sleep(.8)
                sample(scale)
        command("scale", .75)
        sample(.75)
        command("quit")
        process.wait(timeout=20)
        (root / "page.json").unlink()
        process = subprocess.Popen([str(args.reaper), "-newinst", "-cfgfile", str(root / "reaper.ini"),
                                    "-new", "-nosplash", str(root / "launch.lua")], stdout=log, stderr=log)
        wait(lambda: (root / "page.json").exists(), 180)
        sample(.75)
        command("switch", 1)
        time.sleep(.8)
        sample(.75)
        command("float", 1)
        time.sleep(.8)
        sample(.75)
        command("quit")
        process.wait(timeout=20)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
(root / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
switches = {}
for phase, width, height, web_width, web_height in csv.reader((root / "trace.csv").open()):
    if "-switch-" in phase:
        switches.setdefault(phase, set()).add((int(width), int(height), int(web_width), int(web_height)))
assert switches and all(len(sizes) == 1 for sizes in switches.values()), switches
print(json.dumps({"profile": str(root), "checks": len(records), "stableSwitches": len(switches), "passed": True}))
