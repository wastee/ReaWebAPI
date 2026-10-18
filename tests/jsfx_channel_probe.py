"""Create EEL fixtures for the opt-in native channel probe in a disposable profile."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('profile', type=Path)
args = parser.parse_args()
effects = args.profile / 'Effects'
effects.mkdir(parents=True, exist_ok=True)
for name, expression in [('probe-control', '73'), ('probe-native-call', 'reaweb_probe_add(70,3)')]:
    (effects / name).write_text(
        f'desc:{name}\nslider1:0<0,100,1>trigger\nslider2:0<0,100,1>result\n@slider\nslider2={expression};\n', encoding='utf-8')
(effects / 'probe-gmem').write_text(
    'desc:probe-gmem\noptions:gmem=ReaWebAPI_P0_Probe\nslider1:0<0,100,1>trigger\n'
    'slider2:0<0,100,1>readback\n@slider\ngmem[0]=37;slider2=gmem[1];\n', encoding='utf-8')
(args.profile / 'enable-channel-probe').touch()
