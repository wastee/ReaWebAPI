const { test } = require('node:test');
const assert = require('node:assert/strict');
const turn = () => new Promise(resolve => setImmediate(resolve));
function fixture() {
  const events = {}, calls = [], blocked = [], errors = [];
  let value = [.2, .3], revision = 0n, activation = 1, holdWrites = false, holdReads = false;
  let gesture, nextGesture = 0;
  const sessionId = 'instance-a';
  const state = (values = value, rev = ++revision, at = activation) => ({ sessionId, fxIdent: 'test', count: 2, activation: at,
    revision: String(rev), active: true, parameters: values.map((v, index) => ({ index, value: v * 2, normalized: v, defaultNormalized: .5 })) });
  const ok = data => ({ ok: true, sessionId, data });
  const invoke = async (method, data) => {
    calls.push({ method, data });
    if (method !== 'getCapabilities') assert.equal(data.sessionId, sessionId);
    if (method === 'getCapabilities') return ok({ protocolVersion: 1, sessionId, capabilityRevision: '1', status: 'parameterOnly', streams: [], dataStates: [], commands: [], resources: [] });
    if (method === 'getParamState') {
      const snapshot = state();
      if (holdReads) return new Promise(resolve => blocked.push({ method, data, resolve: next => resolve(ok(next || snapshot)) }));
      return ok(snapshot);
    }
    if (method === 'present') return ok({ presented: true });
    if (method === 'beginParamEdit' || method === 'beginParamEditGroup') { gesture = `g${++nextGesture}`; return ok({ gestureId: gesture }); }
    if (method === 'endParamEdit' || method === 'endParamEditGroup') { assert.equal(data.gestureId, gesture); gesture = undefined; return ok({ ended: true }); }
    if (method === 'setParam' || method === 'setParams') {
      if (gesture) assert.equal(data.gestureId, gesture);
      const apply = () => {
        const updates = method === 'setParam' ? [data] : data.values;
        for (const item of updates) value[item.index] = item.normalized;
        const parameters = updates.map(p => ({ index: p.index, normalized: p.normalized, value: p.normalized * 2 }));
        return ok({ [method === 'setParam' ? 'parameter' : 'applied']: method === 'setParam' ? parameters[0] : parameters, revision: String(++revision) });
      };
      if (holdWrites) return new Promise(resolve => blocked.push({ method, data, resolve: next => resolve(next || apply()) }));
      return apply();
    }
    throw Error(`Unexpected ${method}`);
  };
  const reaper = { lifecycle: { ready: Promise.resolve({ runtime: { jsfx: { protocolVersion: 1 } } }) }, host: { service: name => {
    assert.equal(name, 'jsfx'); return { invoke, on: async (name, fn) => { events[name] = fn; return async () => { delete events[name]; }; } };
  } } };
  return { events, calls, blocked, errors, state, ok, reaper, sessionId,
    holdWrites: () => { holdWrites = true; }, holdReads: () => { holdReads = true; },
    event: (values, rev, at = 1) => events.paramsChanged({ sessionId, activation: at, revision: String(rev), parameters: state(values, rev, at).parameters }),
    visibility: (active, at, values) => { activation = at; events.visibilityChanged({ sessionId, active, activation: at, ...(active ? { paramState: state(values, ++revision, at) } : {}) }); }
  };
}
async function connect(f = fixture()) {
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  f.fx = await connectJSFX(f.reaper, error => f.errors.push(error)); return f;
}
test('editor size is initialized before connect and gates native presentation', async () => {
  const f = fixture(), base = f.reaper.host.service('jsfx');
  let size = { width: 1380, height: 319, customized: false, revision: '1' }, complete;
  f.reaper.host.service = () => ({ ...base, invoke: async (method, data) => {
    if (method === 'getCapabilities') {
      const result = await base.invoke(method, data);
      result.data.features = { editorSize: true }; return result;
    }
    if (method === 'getEditorSize') return f.ok(size);
    if (method === 'setEditorSize') {
      assert.equal(data.activation, 1);
      return new Promise(resolve => { complete = () => {
        size = { width: data.width, height: data.height, customized: true, revision: '2' };
        resolve(f.ok(size));
      }; });
    }
    return base.invoke(method, data);
  } });
  await connect(f);
  assert.equal(f.fx.editor.size.customized, false);
  const pending = f.fx.editor.setSize(1035, 239);
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(f.calls.some(call => call.method === 'present'), false);
  complete(); await pending;
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(f.calls.some(call => call.method === 'present'), true);
  assert.equal(f.fx.editor.size.width, 1035);
  const sizes = [];
  await f.fx.editor.onSizeChange(size => sizes.push(size.width));
  f.events.editorSizeChanged({ sessionId: f.sessionId, size: { ...size, width: 690, revision: '3' } });
  f.events.editorSizeChanged({ sessionId: f.sessionId, size });
  f.events.editorSizeChanged({ sessionId: 'other', size: { ...size, width: 2070, revision: '4' } });
  assert.deepEqual(sizes, [690]);
  f.visibility(false, 2);
  await assert.rejects(f.fx.editor.setSize(1380, 319), { code: 'FX_STALE' });
  await assert.rejects(f.fx.editor.setSize(99, 319), { code: 'INVALID_ARGUMENT' });
  await f.fx.dispose();
});
test('editor sizing remains optional with older runtimes', async () => {
  const f = await connect();
  assert.equal(f.fx.editor.size, undefined);
  await assert.rejects(f.fx.editor.getSize(), { code: 'UNSUPPORTED' });
  await assert.rejects(f.fx.editor.setSize(640, 420), { code: 'UNSUPPORTED' });
  await f.fx.dispose();
});
function nativeFixture() {
  const f=fixture(), original=f.reaper.host.service('jsfx');
  let session=f.sessionId, capRevision='1';
  f.nativeCalls=[];
  const result=data=>({ok:true,sessionId:session,data});
  f.reaper.host.service=()=>({...original,invoke:async(method,data)=>{
    if(method==='getCapabilities')return result({protocolVersion:1,sessionId:session,capabilityRevision:capRevision,status:'ready',
      streams:[{id:'output'}],dataStates:[{id:'constructor'}],commands:[{id:'constructor'}],resources:[]});
    if(['getParamState','present','beginParamEdit','setParam','endParamEdit'].includes(method)) {
      const reply=await original.invoke(method,{...data,sessionId:f.sessionId});
      return {...reply,sessionId:session,data:reply.data.sessionId?{...reply.data,sessionId:session}:reply.data};
    }
    assert.equal(data.sessionId,session);f.nativeCalls.push({method,data});
    if(method==='getDataState')return result({id:data.id,revision:'9007199254740993',schemaVersion:1,value:[.1],available:true});
    if(method==='openStream')return result({streamName:'private-test',descriptor:{sessionId:session}});
    if(method==='findOperation')return result({operation:null});
    return result({operationId:'operation',requestId:data.requestId,revision:'1',status:'queued'});
  }});
  let closed=false,closedCallback;
  f.stream={get closed(){return closed;},on:(event,fn)=>{if(event==='close')closedCallback=fn;return()=>{};},close:async()=>{closed=true;closedCallback?.();}};
  f.reaper.stream={open:async name=>{assert.equal(name,'private-test');return f.stream;}};
  f.replaceSession=()=>{session='instance-new';capRevision='1';f.events.capabilitiesChanged({sessionId:session,capabilityRevision:capRevision,reason:'sessionChanged'});};
  return f;
}
test('native namespaces preserve request IDs, independent revisions and stream ownership',async()=>{
  const f=await connect(nativeFixture());
  assert.equal((await f.fx.dataState.get('constructor')).revision,'9007199254740993');
  await f.fx.dataState.set('constructor',[.2],{expectedRevision:'9007199254740993',requestId:'retry-me'});
  assert.deepEqual(f.nativeCalls.at(-1),{method:'setDataState',data:{id:'constructor',value:[.2],expectedRevision:'9007199254740993',requestId:'retry-me',activation:1,sessionId:f.sessionId}});
  await f.fx.commands.invoke('constructor',{}, {requestId:'command'});
  await assert.rejects(f.fx.commands.invoke('undeclared',{}),{code:'CAPABILITY_UNAVAILABLE'});
  const changes=[];const off=await f.fx.operations.onChange(event=>changes.push(event));
  f.events.operationChanged({sessionId:f.sessionId,operationId:'o',revision:'10',status:'running'});
  f.events.operationChanged({sessionId:f.sessionId,operationId:'o',revision:'9',status:'queued'});
  assert.equal(changes.length,1);await off();
  const stream=await f.fx.streams.open('output');assert.equal(stream,f.stream);
  f.visibility(false,2);await turn();assert(stream.closed);
  await f.fx.dispose();assert.equal(Object.keys(f.events).length,0);
});
test('continuous command ownership is released on deactivation and disposal',async()=>{
  const f=nativeFixture(), original=f.reaper.host.service();
  f.reaper.host.service=()=>({...original,invoke:async(method,data)=>{
    const result=await original.invoke(method,data);
    if(method==='getCapabilities')result.data.commands[0].continuous=true;
    return result;
  }});
  await connect(f);
  await f.fx.commands.invoke('constructor',{}, {requestId:'hold'});
  f.visibility(false,2);await turn();
  assert(f.nativeCalls.some(call=>call.method==='releaseCommands'));
  await f.fx.commands.release();
  const before=f.nativeCalls.filter(call=>call.method==='releaseCommands').length;
  await f.fx.dispose();
  assert.equal(f.nativeCalls.filter(call=>call.method==='releaseCommands').length,before+1);
});

test('blur rejects delayed continuous invocations until focus returns',async()=>{
  const handlers=new Map(),f=nativeFixture(),original=f.reaper.host.service();
  const add=globalThis.addEventListener,remove=globalThis.removeEventListener;
  globalThis.addEventListener=(name,fn)=>handlers.set(name,fn);
  globalThis.removeEventListener=name=>handlers.delete(name);
  f.reaper.host.service=()=>({...original,invoke:async(method,data)=>{
    const result=await original.invoke(method,data);
    if(method==='getCapabilities')result.data.commands[0].continuous=true;
    return result;
  }});
  try {
    await connect(f);handlers.get('blur')();await turn();
    assert(f.nativeCalls.some(call=>call.method==='releaseCommands'));
    await assert.rejects(f.fx.commands.invoke('constructor',{}),{code:'FX_STALE'});
    handlers.get('focus')();await f.fx.commands.invoke('constructor',{}, {requestId:'focused'});
    await f.fx.dispose();assert.equal(handlers.size,0);
  } finally {
    globalThis.addEventListener=add;globalThis.removeEventListener=remove;
  }
});

test('EEL runtime replacement re-handshakes without mixing old revisions or streams',async()=>{
  const f=await connect(nativeFixture());
  const stream=await f.fx.streams.open('output');
  f.replaceSession();await turn();await turn();
  assert.equal(f.fx.params.snapshot.sessionId,'instance-new');assert(stream.closed);
  assert.equal((await f.fx.getCapabilities()).sessionId,'instance-new');
  await f.fx.dataState.get('constructor');assert.equal(f.nativeCalls.at(-1).data.sessionId,'instance-new');
  await f.fx.dispose();assert.deepEqual(f.errors,[]);
});

test('resource SDK keeps bytes off service JSON, retries identity and releases document transfers',async()=>{
  const f=nativeFixture(), service=f.reaper.host.service('jsfx'), requests=[], packets=[];
  let downloadPolls=0, reconnect=true, corrupt=false;
  const upload='a'.repeat(64), download='b'.repeat(64);
  const descriptor=id=>({transferId:id,maxChunkBytes:262144,transport:{mode:'jsfxResourceV1',url:'ws://127.0.0.1/private-ticket'}});
  f.reaper.host.service=()=>({...service,invoke:async(method,data)=>{
    if(method==='getCapabilities') {
      const reply=await service.invoke(method,data);reply.data.resources=[{id:'asset',format:'bytes',maxBytes:32768}];return reply;
    }
    requests.push({method,data});
    const result=value=>({ok:true,sessionId:f.sessionId,data:value});
    if(method==='beginResourceUpload')return result(descriptor(upload));
    if(method==='openResourceDownload') {
      if(!downloadPolls++)return result({pending:true,transferId:download});
      return result({...descriptor(download),revision:'2',totalBytes:3,format:'bytes',metadata:{name:'abc'},checksum:'b'.repeat(64)});
    }
    if(method==='abortResourceUpload')return result({aborted:true});
    if(method==='closeResourceDownload')return result({closed:true});
    return service.invoke(method,data);
  }});
  const originalSocket=globalThis.WebSocket;
  globalThis.WebSocket=class {
    constructor(){this.readyState=0;queueMicrotask(()=>{this.readyState=1;this.onopen?.();});}
    close(){this.readyState=3;queueMicrotask(()=>this.onclose?.());}
    send(bytes) {
      const packet=bytes.slice(), view=new DataView(packet.buffer);packets.push(packet);
      assert.equal(view.getBigUint64(16,true),0n);
      if(packet[4]===1) {
        assert.deepEqual([...packet.subarray(104)],[97,98,99]);assert.equal(view.getUint32(96,true),0x352441c2);
        if(reconnect){reconnect=false;this.close();return;}
      }
      const reply=new Uint8Array(packet[4]===1?104:107);reply.set(packet.subarray(0,104));reply[5]=2;
      const out=new DataView(reply.buffer);out.setUint32(96,0,true);
      if(packet[4]===2){reply.set([97,98,99],104);out.setUint32(96,corrupt?0:0x352441c2,true);}
      queueMicrotask(()=>this.onmessage?.({data:reply.buffer}));
    }
  };
  try {
    await connect(f);
    const begun=await f.fx.resources.beginUpload('asset',{format:'bytes',totalBytes:3,requestId:'retry-upload'});
    assert.deepEqual(Object.keys(begun).sort(),['maxChunkBytes','transferId']);
    assert.deepEqual(await f.fx.resources.writeChunk(begun.transferId,0,new Uint8Array([97,98,99])),{offset:0,acceptedBytes:3});
    assert.equal(requests.filter(r=>r.method==='beginResourceUpload').length,2);
    assert(requests.filter(r=>r.method==='beginResourceUpload').every(r=>r.data.requestId==='retry-upload'));
    assert.equal(new DataView(packets[0].buffer).getBigUint64(8,true),new DataView(packets[1].buffer).getBigUint64(8,true));
    await f.fx.resources.commitUpload(upload,{expectedRevision:'1',checksum:'a'.repeat(64),requestId:'commit-once'});
    await assert.rejects(f.fx.resources.writeChunk(upload,0,new Uint8Array()),{code:'TRANSFER_NOT_FOUND'});
    const snapshot=await f.fx.resources.openDownload('asset');assert.equal(snapshot.metadata.name,'abc');assert(!('transport' in snapshot));
    assert.deepEqual([...await f.fx.resources.readChunk(snapshot.transferId,0,3)],[97,98,99]);
    corrupt=true;await assert.rejects(f.fx.resources.readChunk(snapshot.transferId,0,3),{code:'PROTOCOL_MISMATCH'});
    f.visibility(false,2);await turn();
    assert(requests.some(r=>r.method==='closeResourceDownload'&&r.data.transferId===download));
    assert(requests.every(r=>!('bytes' in r.data)));
    await f.fx.dispose();
  } finally {globalThis.WebSocket=originalSocket;}
});
test('namespace contract, immutable snapshots, protocol detection and unsupported extended capabilities', async () => {
  const f = await connect();
  assert.deepEqual(Object.keys(f.fx).sort(), ['editor','params','dataState','streams','commands','operations','resources','active','getCapabilities','onCapabilitiesChange','getDiagnostics','present','onVisibilityChange','dispose'].sort());
  assert(Object.isFrozen(f.fx.params.snapshot.parameters[0]));
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, .2);
  assert.equal(f.calls.filter(c => c.method === 'setParam').length, 0);
  await assert.rejects(f.fx.dataState.get('curve'), { code: 'UNSUPPORTED' });
  await assert.rejects(f.fx.resources.writeChunk('t', 0, new Uint8Array()), { code: 'UNSUPPORTED' });
  await f.fx.dispose();
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  await assert.rejects(connectJSFX({ lifecycle: { ready: Promise.resolve({}) } }), { code: 'UNSUPPORTED' });
  await assert.rejects(connectJSFX({ lifecycle: { ready: Promise.resolve({ runtime: { jsfx: { protocolVersion: 2 } } }) } }), { code: 'PROTOCOL_MISMATCH' });
});
test('rapid gesture writes resolve superseded calls and drain the final value before ending', async () => {
  const f = await connect(); f.holdWrites(); await f.fx.params.beginEdit(0);
  const first = f.fx.params.set(0, .1); await turn();
  const rest = [];
  for (let n = 0; n <= 1000; ++n) rest.push(f.fx.params.set(0, n / 1000));
  assert.equal(f.blocked.length, 1);
  for (const result of await Promise.all(rest.slice(0, -1))) assert.deepEqual(result, { applied: false, superseded: true });
  const end = f.fx.params.endEdit();
  f.blocked.shift().resolve(); await first; await turn();
  assert.equal(f.blocked[0].data.normalized, 1);
  assert.equal(f.calls.filter(c => c.method === 'endParamEdit').length, 0);
  f.blocked.shift().resolve(); await rest.at(-1); await end;
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, 1);
  assert.equal(f.calls.filter(c => c.method === 'setParam').length, 2);
  await f.fx.dispose();
});
test('newer events and snapshots supersede late writes with decimal revisions beyond Number precision', async () => {
  for (const gesture of [false, true]) {
    const f = await connect(); f.holdWrites();
    if (gesture) await f.fx.params.beginEdit(0);
    const write = f.fx.params.set(0, .4); await turn();
    f.event([.7,.3], '9007199254740993');
    f.blocked.shift().resolve(f.ok({ parameter: { index: 0, normalized: .4 }, revision: '9007199254740992' })); await write;
    assert.equal(f.fx.params.snapshot.parameters[0].normalized, .7);
    assert.equal(f.fx.params.snapshot.revision, '9007199254740993');
    await f.fx.dispose();
  }
  const f = await connect(); f.event([.9,.3], '9'); f.event([1,.3], '10');
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, 1); await f.fx.dispose();
});
test('subscription precedes initial snapshot and preserves a newer delta', async () => {
  const f = fixture(); f.holdReads();
  const ready = connect(f); await turn();
  f.event([.7,.3], '3'); f.blocked.shift().resolve(f.state([.4,.3], '2'));
  await ready; assert.equal(f.fx.params.snapshot.parameters[0].normalized, .7);
  assert.equal(f.fx.params.snapshot.revision, '3'); await f.fx.dispose();
});

test('out-of-order deltas and snapshots preserve unrelated parameter updates', async () => {
  const f = await connect();
  f.events.paramsChanged({ sessionId: f.sessionId, activation: 1, revision: '10', parameters: [{ index: 0, normalized: .8 }] });
  f.events.paramsChanged({ sessionId: f.sessionId, activation: 1, revision: '9', parameters: [{ index: 1, normalized: .7 }] });
  assert.deepEqual(f.fx.params.snapshot.parameters.map(p => p.normalized), [.8,.7]);
  f.holdReads();
  const read = f.fx.params.getState();
  f.blocked.shift().resolve(f.state([.2,.9], '9'));
  await read;
  assert.deepEqual(f.fx.params.snapshot.parameters.map(p => p.normalized), [.8,.9]);
  assert.equal(f.fx.params.snapshot.revision, '10');
  await f.fx.dispose();
});

test('disposal accepts a destroyed binding and disposed reads do not send requests', async () => {
  const f = fixture();
  const service = f.reaper.host.service('jsfx');
  f.reaper.host.service = () => ({ ...service, invoke: (method, data) => method === 'endParamEdit'
    ? Promise.resolve({ ok: false, error: { code: 'FX_STALE', message: 'FX deleted' } }) : service.invoke(method, data) });
  await connect(f); await f.fx.params.beginEdit(0);
  const disposal = f.fx.dispose(); assert.equal(f.fx.dispose(), disposal); await disposal;
  const count = f.calls.length;
  await assert.rejects(f.fx.params.getState(), { code: 'FX_UNBOUND' });
  assert.equal(f.calls.length, count); assert.deepEqual(f.events, {});
});
test('host calibration, discrete writes, reset and asynchronous idempotent subscriptions', async () => {
  const f = await connect(); const seen = [];
  const off = await f.fx.params.onChange(state => seen.push(state));
  assert.deepEqual(await f.fx.params.set(0, .4), { applied: true, superseded: false, parameter: { index: 0, normalized: .4, value: .8 }, revision: '2' });
  await f.fx.params.set(0, .8); await f.fx.params.reset(0);
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, .5);
  assert.equal(f.calls.filter(c => c.method === 'setParam').length, 3);
  assert(seen.length); await off(); await off(); const count = seen.length;
  f.event([.7,.3], '100'); assert.equal(seen.length, count); await f.fx.dispose();
});
test('stale activations reject pending writes, preserve new state and render before present', async () => {
  const f = await connect(); f.holdWrites(); f.holdReads();
  const order = [], visible = [];
  await f.fx.params.onChange(() => order.push('render'));
  await f.fx.onVisibilityChange(active => visible.push(active));
  const read = f.fx.params.getState(); const oldRead = f.blocked.shift();
  await f.fx.params.beginEdit(0);
  const write = f.fx.params.set(0, .9); const rejectedWrite = assert.rejects(write, { code: 'FX_STALE' }); await turn();
  const queued = f.fx.params.set(0, .8); const rejectedQueued = assert.rejects(queued, { code: 'FX_STALE' });
  f.visibility(false, 2); await assert.rejects(f.fx.params.set(0, .7), { code: 'FX_STALE' });
  f.visibility(true, 3, [.4,.3]); f.blocked.shift().resolve(); oldRead.resolve(f.state([.1,.3], '100', 1));
  await Promise.all([rejectedWrite, rejectedQueued, read]); await turn();
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, .4); assert.deepEqual(visible, [false,true]);
  assert.equal(order[0], 'render'); assert.equal(f.calls.at(-1).method, 'present');
  f.events.paramsChanged({ sessionId: 'other', activation: 3, revision: '1000', parameters: [{ index: 0, normalized: .9 }] });
  assert.equal(f.fx.params.snapshot.parameters[0].normalized, .4); await f.fx.dispose();
});
test('single and group gestures are mutually exclusive, validate atomically and keep final values', async () => {
  const f = await connect();
  await f.fx.params.beginEditGroup([0,1]);
  await assert.rejects(f.fx.params.beginEdit(0), { code: 'GESTURE_BUSY' });
  await assert.rejects(f.fx.params.set(0, .9), { code: 'GESTURE_BUSY' });
  await assert.rejects(f.fx.params.reset(0), { code: 'GESTURE_BUSY' });
  await assert.rejects(f.fx.params.setMany([{ index: 0, normalized: .1 }, { index: 1, normalized: 2 }]), { code: 'INVALID_ARGUMENT' });
  await assert.rejects(f.fx.params.setMany([{ index: 0, normalized: .1 }, { index: 0, normalized: .2 }]), { code: 'INVALID_ARGUMENT' });
  await assert.rejects(f.fx.params.setMany([null]), { code: 'INVALID_ARGUMENT' });
  await f.fx.params.setMany([{ index: 0, normalized: .1 }, { index: 1, normalized: .6 }]);
  await f.fx.params.endEditGroup(); assert.deepEqual(f.fx.params.snapshot.parameters.map(p => p.normalized), [.1,.6]);
  assert.equal(f.calls.filter(c => c.method === 'setParams').length, 1); await f.fx.dispose();
});
test('failure rejects writes without hiding business codes and disposal always unsubscribes', async () => {
  const f = await connect(); f.holdWrites(); await f.fx.params.beginEdit(0);
  const write = f.fx.params.set(0, .4); await turn();
  f.blocked.shift().resolve({ ok: false, sessionId: f.sessionId, error: { code: 'INVALID_ARGUMENT', message: 'bad', details: { index: 0 } } });
  await assert.rejects(write, { code: 'INVALID_ARGUMENT', details: { index: 0 } });
  await assert.rejects(f.fx.params.endEdit(), { code: 'INVALID_ARGUMENT' });
  const disposal = f.fx.dispose(); assert.equal(f.fx.dispose(), disposal); await disposal;
  assert.deepEqual(f.events, {});
});
test('bounded discrete queues reject overflow instead of silently merging independent writes', async () => {
  const f = await connect(); f.holdWrites();
  const writes = [f.fx.params.set(0, .1)]; await turn();
  for (let n = 0; n < 16; ++n) writes.push(f.fx.params.set(0, .2));
  await assert.rejects(f.fx.params.set(0, .3), { code: 'QUEUE_LIMIT' });
  for (let n = 0; n < 17; ++n) { f.blocked.shift().resolve(); await writes[n]; await turn(); }
  assert.equal(f.calls.filter(c => c.method === 'setParam').length, 17); await f.fx.dispose();
});

test('independent subscriptions start together and snapshots wait for every registration', async () => {
  for (const sizing of [false, true]) {
    const f = fixture(), base = f.reaper.host.service('jsfx'), pending = [], registered = [];
    f.reaper.host.service = () => ({
      invoke: async (method, data) => {
        if (method === 'getEditorSize') return f.ok({width:600,height:300,revision:'1'});
        const reply = await base.invoke(method,data);
        if (method === 'getCapabilities') reply.data.features = {editorSize:sizing};
        return reply;
      },
      on: (name, fn) => {
        registered.push(name); f.events[name] = fn;
        return new Promise(resolve => pending.push(() => resolve(async () => { delete f.events[name]; })));
      }
    });
    const connecting = connect(f);
    await turn();
    assert.deepEqual(new Set(registered), new Set(['paramsChanged','capabilitiesChanged','dataStateChanged','operationChanged','visibilityChanged', ...(sizing?['editorSizeChanged']:[])]));
    assert.equal(f.calls.filter(c=>c.method==='getParamState').length,0);
    f.event([.7,.8], '50');
    if (sizing) f.events.editorSizeChanged({sessionId:f.sessionId,size:{width:900,height:400,revision:'2'}});
    for (const finish of pending.slice(1)) finish();
    await turn();
    assert.equal(f.calls.filter(c=>c.method==='getParamState').length,0);
    pending[0](); await connecting;
    assert.equal(f.fx.params.snapshot.parameters[0].normalized,.7);
    if (sizing) assert.equal(f.fx.editor.size.width,900);
    await f.fx.dispose(); assert.deepEqual(Object.keys(f.events),[]);
  }
});

test('failed connection invalidates callbacks and disposes early and late registrations', async () => {
  const f = fixture(), base = f.reaper.host.service('jsfx'), pending = new Map(), stopped = [];
  f.reaper.host.service = () => ({...base,on:(name,fn)=>{
    f.events[name]=fn;
    return new Promise((resolve,reject)=>pending.set(name,{resolve:()=>resolve(()=>{stopped.push(name);delete f.events[name];}),reject}));
  }});
  const {connectJSFX} = await import('../runtime/jsfx-webui.mjs');
  const opening = connectJSFX(f.reaper,error=>f.errors.push(error));
  const rejected = assert.rejects(opening, /subscription failed/);
  await turn();
  pending.get('paramsChanged').resolve(); await turn();
  pending.get('operationChanged').reject(new Error('subscription failed')); delete f.events.operationChanged;
  await rejected;
  assert.deepEqual(stopped,['paramsChanged']);
  const calls = f.calls.length;
  f.events.visibilityChanged({sessionId:f.sessionId,activation:2,active:true,paramState:f.state([.8,.8],20,2)});
  f.events.capabilitiesChanged({sessionId:f.sessionId,capabilityRevision:'2'});
  for (const [name,p] of pending) if (!['paramsChanged','operationChanged'].includes(name)) p.resolve();
  await turn(); await turn();
  assert.equal(f.calls.length,calls);
  assert.deepEqual(Object.keys(f.events),[]);
  assert.equal(new Set(stopped).size,4);
  assert.deepEqual(f.errors,[]);
});

test('visibility during registration cannot present before size and parameter initialization', async () => {
  const f=fixture(),base=f.reaper.host.service('jsfx'); let release;
  f.reaper.host.service=()=>({...base,on:async(name,fn)=>{
    const off=await base.on(name,fn);
    if(name==='visibilityChanged') {
      fn({sessionId:f.sessionId,activation:2,active:true,paramState:f.state([.9,.8],30,2)});
      await new Promise(resolve=>{release=resolve;});
    }
    return off;
  }});
  const opening=connect(f); await turn(); await turn();
  assert.equal(f.calls.some(c=>c.method==='present'),false);
  release(); await opening;
  await new Promise(resolve=>setTimeout(resolve,10));
  assert.equal(f.fx.params.snapshot.activation,2);
  assert.equal(f.calls.find(c=>c.method==='present').data.activation,2);
  await f.fx.dispose();
});
