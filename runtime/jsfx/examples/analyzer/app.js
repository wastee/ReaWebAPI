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
let streams=[],opening=false;
el('tone').checked=jsfx.params.snapshot.parameters[0].normalized>0.5;
el('tone').onchange=action(()=>jsfx.params.set(0,el('tone').checked?1:0));
await jsfx.params.onChange(s=>{el('tone').checked=s.parameters[0].normalized>0.5;});
function draw(id,stream,packet){
  if(el('freeze').checked)return;
  const canvas=el(id),ctx=canvas.getContext('2d'),data=packet.data,channels=stream.info.channels;
  ctx.clearRect(0,0,canvas.width,canvas.height);ctx.strokeStyle='#8ed6b6';ctx.beginPath();
  const stride=id==='waveform'?channels*2:channels,count=data.length/stride;
  for(let i=0;i<count;i++){
    const x=i*canvas.width/Math.max(1,count-1),value=data[i*stride];
    if(id==='waveform'){ctx.moveTo(x,65-value*60);ctx.lineTo(x,65-data[i*stride+1]*60);}
    else{const y=id==='spectrum'?130-Math.max(0,1+20*Math.log10(Math.max(1e-8,value))/90)*130:65-value*60;i?ctx.lineTo(x,y):ctx.moveTo(x,y);}
  }
  ctx.stroke();status.textContent=`${id} · sample ${packet.timing?.firstSample} · dropped ${packet.timing?.sourceDropped}`;
}
async function open(){
  if(opening||!jsfx.active||streams.some(s=>!s.closed))return;
  opening=true;
  try{
    if((await jsfx.getCapabilities()).status!=='ready')return;
    for(const id of ['spectrum','waveform','scope']){
      const stream=await jsfx.streams.open(id);streams.push(stream);
      stream.on('data',packet=>{draw(id,stream,packet);if(id==='scope')while(stream.read()){};});stream.on('error',report);
    }
  }catch(error){await Promise.allSettled(streams.map(s=>s.close()));streams=[];report(error);}finally{opening=false;}
}
await jsfx.onVisibilityChange(active=>{if(active)open();else streams=[];});
await jsfx.onCapabilitiesChange(()=>{streams=streams.filter(s=>!s.closed);open();});
await open();setInterval(()=>open(),1000);
