const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync(require('node:path').join(__dirname,'../src/platform/shared/page_background.hpp'),'utf8');
// Execute exactly the embedded Windows script, replacing only its native post.
const script=[...source.matchAll(/R"JS\(([\s\S]*?)\)JS"/g)].map(x=>x[1]).join('')
  .replace('const post = message => {  };','const post = message => messages.push(message);');
function fixture(rootColor,bodyColor,rootImage='none',pixels=[1,2,3,255]) {
  const messages=[],frames=[],calls=[],events={},measures=[];
  const root={backgroundColor:rootColor,backgroundImage:rootImage};
  const body={backgroundColor:bodyColor};
  const document={readyState:'complete',documentElement:root,body,head:{},createElement:name=>{
    calls.push('create:'+name);
    return {getContext:()=>({clearRect:()=>calls.push('clear'),fillRect:()=>calls.push('fill'),
      getImageData:()=>{calls.push('read');return {data:pixels};}})};
  }};
  vm.runInNewContext(script,{document,messages,performance:{now:()=>0,measure:name=>measures.push(name)},
    getComputedStyle:x=>x,requestAnimationFrame:fn=>frames.push(fn),
    MutationObserver:class {observe(){} disconnect(){}},
    addEventListener:(name,fn)=>(events[name]??=[]).push(fn),
    matchMedia:()=>({addEventListener(){}})});
  const flush=()=>{for(const fn of frames.splice(0))fn();};flush();
  return {messages,calls,measures,root,body,update:()=>{events.resize.forEach(fn=>fn());flush();}};
}
test('computed opaque RGB and transparent root do not initialize Canvas',()=>{
  const f=fixture('rgba(0, 0, 0, 0)','rgb(24, 40, 56)');
  assert.deepEqual(f.messages,['__reawebBackground:'+0x182838]);
  assert.deepEqual(f.calls,[]);
  f.body.backgroundColor='rgb(232, 216, 200)';f.update();
  assert.equal(f.messages.at(-1),'__reawebBackground:'+0xe8d8c8);
  f.root.backgroundColor='rgba(12, 34, 56, 1)';f.update();
  assert.equal(f.messages.at(-1),'__reawebBackground:'+0x0c2238);
  assert.deepEqual(f.calls,[]);
  assert.equal(new Set(f.measures).size,f.measures.length);
});
test('root image prevents body fallback and transparent backgrounds remain white',()=>{
  assert.deepEqual(fixture('rgba(0, 0, 0, 0)','rgb(24, 40, 56)','url(image)').messages,['__reawebBackground:16777215']);
  assert.deepEqual(fixture('rgba(0, 0, 0, 0)','rgba(0, 0, 0, 0)').messages,['__reawebBackground:16777215']);
});
test('fractional alpha and nonstandard CSS keep original Canvas rounding and conversion',()=>{
  for(const [css,pixel,expected] of [
    ['rgba(10, 20, 30, 0.5)',[10,20,30,128],0xffffff],
    ['rgba(10, 20, 30, 0.999)',[10,20,30,255],0x0a141e],
    ['color(display-p3 1 0 0)',[255,0,0,255],0xff0000],
    ['rgb(1.5, 2, 3)',[2,2,3,255],0x020203]
  ]) {
    const f=fixture(css,'rgb(0, 0, 0)','none',pixel);
    assert.equal(f.messages[0],'__reawebBackground:'+expected);
    assert.deepEqual(f.calls.slice(0,4),['create:canvas','clear','fill','read']);
    f.update();assert.equal(f.calls.filter(x=>x==='create:canvas').length,1);
  }
});
