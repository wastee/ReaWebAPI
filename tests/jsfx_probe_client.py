from pathlib import Path
import json,sys,time,uuid

def call(root,op,**kwargs):
    root=Path(root)
    if not (root/"enable-native-probe").is_file():raise ValueError("Expected an opt-in disposable JSFX probe profile")
    request={'id':uuid.uuid4().hex,'op':op,**kwargs}
    temp=root/'native-command.tmp'
    temp.write_text(json.dumps(request),encoding='utf-8')
    for attempt in range(100):
        try:
            temp.replace(root/'native-command.json');break
        except PermissionError:
            if attempt==99:raise
            time.sleep(.005)
    deadline=time.monotonic()+45
    while time.monotonic()<deadline:
        try:
            result=json.loads((root/'native-result.json').read_text(encoding='utf-8'))
            if result['id']==request['id']:
                if 'error' in result:raise RuntimeError(result['error'])
                return result['result']
        except (FileNotFoundError,json.JSONDecodeError):pass
        except OSError as error:
            # A DrvFS read can race with the probe replacing its reply.
            if error.errno!=61:raise
        time.sleep(.02)
    raise TimeoutError(op)

if __name__=='__main__':print(json.dumps(call(sys.argv[1],**json.loads(sys.argv[2])),indent=2))
