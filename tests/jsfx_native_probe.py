"""Install test fixtures in an explicitly supplied disposable profile."""
from pathlib import Path
import argparse, shutil

parser=argparse.ArgumentParser()
parser.add_argument('profile',type=Path)
args=parser.parse_args()
effects=args.profile/'Effects'
effects.mkdir(parents=True,exist_ok=True)
shutil.copy2(Path(__file__).resolve().parents[1]/'runtime/jsfx/reaweb_jsfx.inc',effects)
(effects/'rwa-native-probe').write_text('''desc:rwa-native-probe
options:gmem=ReaWebAPI_Native_Probe_v1
import reaweb_jsfx.inc
slider1:1<0,1,1>telemetry
slider2:0<-70368744177664,70368744177664,1>-ReaWebAPI transport v1
slider3:1<0,1,1>-SDK processing
in_pin:none
out_pin:left
out_pin:right
@init
ext_tail_size=-1;
sample_clock=0; collected=0;
reaweb_jsfx_init(0,65536);
reaweb_jsfx_state(0,1024,2048,5,32,1,0)==1 ? (
  1024[0]=4;1024[1]=0;1024[2]=0.25;1024[3]=0.5;1024[4]=1;
);
reaweb_jsfx_resource(0,131072,196608,3,32768,1)==1 ? (
  131072[0]=1;131072[1]=2;131072[2]=3;
);
reaweb_jsfx_resource(1,262144,327680,0,8192,2);
@slider
slider2=reaweb_jsfx_handshake(slider2);
@block
slider3 ? (
sdk_block_start=time_precise();
slider2=reaweb_jsfx_handshake(slider2);
command=reaweb_jsfx_service();
command == 1 ? (
  reaweb_jsfx_id==5 ? (
    i=0;loop(reaweb_jsfx_length,32768[i]=reaweb_jsfx_value(i);i+=1;);
    reaweb_jsfx_reply(0,32768,reaweb_jsfx_length);
  ) : reaweb_jsfx_id==3 ? (
    32768[0]=reaweb_jsfx_value(2)>0 ? reaweb_jsfx_note_on(reaweb_jsfx_value(0),reaweb_jsfx_value(1),reaweb_jsfx_value(2)) : reaweb_jsfx_note_off(reaweb_jsfx_value(0),reaweb_jsfx_value(1));
    reaweb_jsfx_reply(0,32768,1);
  ) : reaweb_jsfx_id==4 ? (
    32768[0]=reaweb_jsfx_note_count();reaweb_jsfx_reply(0,32768,1);
  ) : reaweb_jsfx_id>0 ? remaining=reaweb_jsfx_value(0) : (
  i=0;total=0;
  loop(reaweb_jsfx_length,total+=reaweb_jsfx_value(i);i+=1;);
  32768[0]=total;
  reaweb_jsfx_reply(0,32768,1);
  );
);
remaining>0 ? (
  remaining-=1;
  reaweb_jsfx_id==1 && reaweb_jsfx_cancelled() ? (remaining=0;reaweb_jsfx_reply(6,0,0);) :
  remaining==0 ? (32768[0]=0.75;reaweb_jsfx_reply(0,32768,1););
);
16380[0]=time_precise();16380[1]=(16380[0]-sdk_block_start)*1000;
reaweb_jsfx_publish(1,16380,2,1,sample_clock);
);
@sample
sample=sin(sample_clock*2*$pi*440/srate);
spl0=spl1=sample*0.00001;
0[collected*2]=sample;0[collected*2+1]=sample;
sample_clock+=1;collected+=1;
collected == 256 ? (
  slider1 && slider3 ? reaweb_jsfx_publish(0,0,512,2,sample_clock-256);
  collected=0;
);
@serialize
reaweb_jsfx_serialize(0,1,3);
slider2=reaweb_jsfx_handshake(slider2);
''',encoding='utf-8')
(args.profile/'enable-native-probe').touch()
(effects/'rwa-midi-observer').write_text('''desc:rwa-midi-observer
slider1:0<0,100000,1>Note on count
slider2:0<0,100000,1>Note off count
slider3:0<-100000,100000,1>Note balance
slider4:0<0,100000,1>Global clear count
@block
while(midirecv(offset,msg1,msg2,msg3)) (
  (msg1&240)==144 && msg3>0 ? (slider1+=1;slider3+=1;);
  (msg1&240)==128 || ((msg1&240)==144 && msg3==0) ? (slider2+=1;slider3-=1;);
  (msg1&240)==176 && (msg2==120 || msg2==123) ? slider4+=1;
  midisend(offset,msg1,msg2,msg3);
);
''',encoding='utf-8')
