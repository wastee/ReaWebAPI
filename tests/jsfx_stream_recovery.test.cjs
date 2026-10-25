const { test } = require('node:test');
const assert = require('node:assert/strict');
const turn = () => new Promise(resolve => setImmediate(resolve));
const error = code => Object.assign(new Error(code), { code });

async function fixture(t) {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const events = {}, raws = [], calls = [], errors = [];
  let session = 'one', status = 'ready', revision = 1, active = true, activation = 1, lifecycle;
  const f = { events, raws, calls, errors, fail: null, gate: null };
  const service = {
    on: async (name, fn) => { events[name] = fn; return () => { delete events[name]; }; },
    invoke: async (method, data) => {
      calls.push({ method, data });
      if (f.fail) throw error(f.fail);
      if (data.sessionId && data.sessionId !== session) return { ok: false, error: error('STALE_SESSION') };
      let value;
      if (method === 'getCapabilities') value = { protocolVersion: 1, sessionId: session, capabilityRevision: String(revision), status,
        streams: [{ id: 'output' }], commands: [], resources: [], dataStates: [] };
      else if (method === 'getParamState') value = { sessionId: session, revision: '1', activation, active, count: 0, parameters: [] };
      else if (method === 'openStream') value = { streamName: session };
      else if (method === 'present') value = {};
      else throw new Error(method);
      return { ok: true, sessionId: session, data: value };
    }
  };
  const reaper = {
    lifecycle: { ready: Promise.resolve({ runtime: { jsfx: { protocolVersion: 1 } } }),
      on: async (name, fn) => { assert.equal(name, 'cleanup'); lifecycle = fn; return () => { lifecycle = null; }; } },
    host: { service: () => service },
    stream: { open: async name => {
      const handlers = new Map(); let closed = false, last = null;
      const raw = { info: Object.freeze({ name, jsfx: { sessionId: name }, channels: 2 }), dropped: 0,
        get closed() { return closed; }, latest: () => last, read: () => last,
        on(event, fn) {
          if (closed) { if (event === 'close') fn(error('STREAM_CLOSED')); return () => {}; }
          if (!handlers.has(event)) handlers.set(event, new Set()); handlers.get(event).add(fn);
          return () => handlers.get(event).delete(fn);
        },
        emit(event, value) { for (const fn of [...(handlers.get(event) || [])]) fn(value); },
        data(value) { last = value; raw.emit('data', value); },
        close: async () => { if (closed) return; closed = true; last = null; raw.emit('close', error('STREAM_CLOSED')); handlers.clear(); },
        handlers
      };
      raws.push(raw);
      if (f.gate) await f.gate(raw);
      return raw;
    } }
  };
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  f.fx = await connectJSFX(reaper, e => errors.push(e));
  f.tick = async ms => { t.mock.timers.tick(ms); await turn(); await turn(); };
  f.replace = (next = 'two') => { session = next; revision = 1; events.capabilitiesChanged({ sessionId: session, capabilityRevision: '1' }); };
  f.suspend = value => { status = value ? 'suspended' : 'ready'; events.capabilitiesChanged({ sessionId: session, capabilityRevision: String(++revision) }); };
  f.visibility = value => { active = value; events.visibilityChanged({ sessionId: session, active, activation: ++activation }); };
  f.cleanup = () => lifecycle();
  t.after(async () => { f.fail = null; await f.fx.dispose(); });
  await f.tick(0);
  return f;
}

test('session replacement retains handles, subscriptions and options, and rejects old packets', async t => {
  const f = await fixture(t), options = { updateRate: 24 }, stream = await f.fx.streams.open('output', options);
  options.updateRate = 99;
  const data = [], closes = []; const off = stream.on('data', p => data.push(p)); stream.on('close', e => closes.push(e));
  const old = f.raws[0], late = [...old.handlers.get('data')][0];
  old.data('first'); f.replace(); late('stale'); assert.equal(stream.latest(), null);
  await f.tick(100);
  assert.equal(f.raws.length, 2); assert(old.closed); assert(!stream.closed);
  assert.equal(stream.info.jsfx.sessionId, 'two');
  f.raws[1].data('second'); assert.deepEqual(data, ['first', 'second']); assert.deepEqual(closes, []);
  assert.equal(f.calls.filter(c => c.method === 'openStream')[1].data.options.updateRate, 24);
  off(); f.raws[1].data('unsubscribed'); assert.deepEqual(data, ['first', 'second']);
  await stream.close(); await f.tick(60000); assert(stream.closed); assert.equal(closes.length, 1); assert.equal(f.raws.length, 2);
  assert.deepEqual(f.errors, []);
});

test('valid silent or suspended streams never reconnect', async t => {
  const f = await fixture(t), stream = await f.fx.streams.open('output');
  f.suspend(true); await f.tick(60000);
  assert.equal(f.raws.length, 1); assert(!stream.closed);
  assert.equal(f.calls.filter(c => c.method === 'openStream').length, 1);
  const data = []; stream.on('data', p => data.push(p)); f.suspend(false); f.raws[0].data('resumed');
  assert.deepEqual(data, ['resumed']);
});

test('closed streams wait at one second intervals for unscheduled DSP and then recover', async t => {
  const f = await fixture(t), stream = await f.fx.streams.open('output');
  f.suspend(true); await f.raws[0].close(); await f.tick(100);
  const count = f.calls.length; await f.tick(999); assert.equal(f.calls.length, count);
  for (let i = 0; i < 8; i++) await f.tick(1000);
  assert.equal(f.raws.length, 1); assert(!stream.closed); assert.deepEqual(f.errors, []);
  f.suspend(false); await f.tick(1000); assert.equal(f.raws.length, 2);
  f.raws[1].data('resumed'); assert.equal(stream.latest(), 'resumed');
});

test('manual close, editor deactivation and lifecycle cleanup cancel scheduled recovery', async t => {
  const f = await fixture(t);
  for (const mode of ['manual', 'editor', 'cleanup']) {
    const stream = await f.fx.streams.open('output'); await f.raws.at(-1).close();
    const before = f.calls.length;
    if (mode === 'manual') await stream.close();
    if (mode === 'editor') { f.visibility(false); await turn(); }
    if (mode === 'cleanup') await f.cleanup();
    await f.tick(60000); assert(stream.closed); assert.equal(f.calls.length, before);
    if (mode === 'editor') { f.visibility(true); await turn(); }
  }
  assert.equal(Object.keys(f.events).length, 0);
});

test('late transport opens are closed before a new session connects', async t => {
  const f = await fixture(t), stream = await f.fx.streams.open('output');
  let release; f.gate = () => new Promise(resolve => { release = resolve; });
  await f.raws[0].close(); await f.tick(100); assert.equal(f.raws.length, 2);
  f.replace('three'); await f.tick(4000); assert.equal(f.raws.length, 2);
  f.gate = null; release(); await turn(); await f.tick(1000);
  assert(f.raws[1].closed); assert.equal(f.raws.length, 3); assert.equal(stream.info.name, 'three');
  assert.equal(f.raws.filter(raw => !raw.closed).length, 1);
});

test('disposal waits for an in-flight reopen and cleans its late connection', async t => {
  const f = await fixture(t); await f.fx.streams.open('output');
  let release; f.gate = () => new Promise(resolve => { release = resolve; });
  await f.raws[0].close(); await f.tick(100);
  const closing = f.fx.dispose(); release(); await closing;
  assert(f.raws.every(raw => raw.closed)); await f.tick(60000); assert.equal(f.raws.length, 2);
});

test('repeated transport failures are bounded and report a terminal error', async t => {
  const f = await fixture(t), stream = await f.fx.streams.open('output'), closes = [];
  stream.on('close', e => closes.push(e)); f.fail = 'TRANSPORT_ERROR'; await f.raws[0].close();
  for (let i = 0; i < 7; i++) await f.tick(4000);
  assert(stream.closed); assert.equal(f.errors.length, 1); assert.equal(closes.length, 1);
  const count = f.calls.length; await f.tick(60000); assert.equal(f.calls.length, count);
});

test('connections that close before delivering data also exhaust the retry budget', async t => {
  const f = await fixture(t), stream = await f.fx.streams.open('output');
  f.gate = raw => raw.close(); await f.raws[0].close();
  for (let i = 0; i < 8; i++) await f.tick(4000);
  assert(stream.closed); assert.equal(f.raws.length, 7); assert.equal(f.errors[0].code, 'STREAM_RECOVERY_FAILED');
});

test('deleted FX and invalid projects terminate recovery without retries', async t => {
  const f = await fixture(t);
  for (const code of ['FX_UNBOUND', 'FX_STALE', 'PROJECT_CHANGED', 'CAPABILITY_UNAVAILABLE']) {
    f.fail = null; const stream = await f.fx.streams.open('output');
    f.fail = code; await f.raws.at(-1).close(); await f.tick(100);
    assert(stream.closed); assert.equal(stream.error.code, code);
    const count = f.calls.length; await f.tick(60000); assert.equal(f.calls.length, count);
  }
});
