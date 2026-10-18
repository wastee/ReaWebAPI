import {connectJSFX} from './jsfx-webui.mjs';
const el=id=>document.getElementById(id), status=el('status');
const report=error=>{status.textContent=`${error.code||'ERROR'}: ${error.message}`;};
const action=fn=>async()=>{try{await fn();}catch(error){report(error);}};
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const jsfx=await connectJSFX(reaper,report);
const wait=async operation=>{
  while(operation.status==='queued'||operation.status==='running'){
    status.textContent=operation.blockedReason||operation.status;
    await pause(50);operation=await jsfx.operations.get(operation.operationId);
  }
  if(operation.status!=='succeeded')throw Object.assign(new Error(operation.error.message),operation.error);
  return operation.result;
};
let state;
const controls=Array.from({length:4},(_,i)=>{const label=document.createElement('label');label.textContent=`Point ${i+1} `;const input=document.createElement('input');input.type='range';input.min=0;input.max=1;input.step=.001;input.oninput=draw;label.append(input);el('points').append(label);return input;});
function draw(){const ctx=el('curve').getContext('2d');ctx.clearRect(0,0,640,200);ctx.strokeStyle='#8ed6b6';ctx.beginPath();controls.forEach((c,i)=>{const x=i*640/3,y=200-Number(c.value)*200;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();}
async function refresh(){state=await jsfx.dataState.get('curve');if(!state.available){status.textContent='Waiting for DSP';return;}state.value.forEach((v,i)=>{controls[i].value=v;});draw();status.textContent=`Revision ${state.revision}`;}
el('refresh').onclick=action(refresh);
el('apply').onclick=action(async()=>{if(!state?.available)throw new Error('DSP state is unavailable');await wait(await jsfx.dataState.set('curve',controls.map(c=>Number(c.value)),{expectedRevision:state.revision}));await refresh();});
await jsfx.dataState.onChange('curve',()=>refresh().catch(report));
await jsfx.onVisibilityChange(active=>{if(active)refresh().catch(report);});
await refresh();
