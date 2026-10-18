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
const hash=async bytes=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),v=>v.toString(16).padStart(2,'0')).join('');
async function snapshot(){return jsfx.resources.openDownload('asset');}
el('sum').onclick=action(async()=>{const result=await wait(await jsfx.commands.invoke('sum',{a:Number(el('a').value),b:Number(el('b').value)}));status.textContent=`DSP result: ${result}`;});
el('upload').onclick=action(async()=>{
  const file=el('file').files[0];if(!file)return;if(file.size>32768)throw new Error('Asset exceeds 32 KiB');
  const old=await snapshot();await jsfx.resources.closeDownload(old.transferId);
  const bytes=new Uint8Array(await file.arrayBuffer()),upload=await jsfx.resources.beginUpload('asset',{format:'bytes',totalBytes:bytes.length,metadata:{name:file.name}});
  try{
    for(let offset=0;offset<bytes.length;offset+=upload.maxChunkBytes)await jsfx.resources.writeChunk(upload.transferId,offset,bytes.slice(offset,offset+upload.maxChunkBytes));
    const result=await wait(await jsfx.resources.commitUpload(upload.transferId,{expectedRevision:old.revision,checksum:await hash(bytes)}));status.textContent=`Stored ${result.totalBytes} bytes · revision ${result.revision}`;
  }finally{await jsfx.resources.abortUpload(upload.transferId);}
});
el('download').onclick=action(async()=>{
  const resource=await snapshot(),bytes=new Uint8Array(resource.totalBytes);
  try{for(let offset=0;offset<bytes.length;offset+=resource.maxChunkBytes)bytes.set(await jsfx.resources.readChunk(resource.transferId,offset,Math.min(resource.maxChunkBytes,bytes.length-offset)),offset);
    if(await hash(bytes)!==resource.checksum)throw new Error('Snapshot checksum mismatch');
    const blob=new Blob([bytes]),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=resource.metadata.name||'asset.bin';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);status.textContent=`Exported ${bytes.length} bytes`;
  }finally{await jsfx.resources.closeDownload(resource.transferId);}
});
status.textContent='Ready';

let held=false,noteQueue=Promise.resolve();
const note=velocity=>{noteQueue=noteQueue.then(()=>jsfx.commands.invoke('note',{channel:0,note:60,velocity})).catch(report);};
el('note').onpointerdown=event=>{if(held)return;held=true;el('note').setPointerCapture(event.pointerId);note(96);};
const release=()=>{if(!held)return;held=false;note(0);};
el('note').onpointerup=el('note').onpointercancel=el('note').onlostpointercapture=release;
el('note').onkeydown=event=>{if((event.key===' '||event.key==='Enter')&&!held){event.preventDefault();held=true;note(96);}};
el('note').onkeyup=event=>{if(event.key===' '||event.key==='Enter'){event.preventDefault();release();}};
window.addEventListener('blur',()=>{held=false;});
