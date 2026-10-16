const { test } = require('node:test');
const assert = require('node:assert/strict');

test('JSFX initialization is read-only and rapid inputs retain the latest value', async () => {
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  let value = 0.25, writes = [], release, revision = 0, listener;
  const state = () => ({ count: 1, revision: ++revision, activation: 2, active: true, parameters: [{ index: 0, value, normalized: value, defaultNormalized: 0.5 }] });
  const service = {
    on: async (event, fn) => { if (event === 'paramChanged') listener = fn; return async () => {}; },
    invoke: async (method, data) => {
      if (method === 'getState') return state();
      if (method === 'setParam') {
        writes.push(data.value);
        if (writes.length === 1) await new Promise(resolve => { release = resolve; });
        value = data.value; return { index: 0, value, normalized: value };
      }
      return true;
    }
  };
  const connection = await connectJSFX({ host: { service: () => service } });
  assert.equal(connection.state.parameters[0].value, 0.25); assert.deepEqual(writes, []);
  await connection.beginEdit(0);
  connection.setParam(0, 0.1);
  for (let n = 0; n <= 1000; n++) connection.setParam(0, n / 1000);
  release(); await connection.endEdit();
  assert.deepEqual(writes, [0.1, 1]); assert.equal(value, 1);
  listener({ activation: 2, revision: ++revision, parameters: [{ index: 0, value: 0.7, normalized: 0.7 }] });
  assert.equal(connection.state.parameters[0].value, 0.7);
  await connection.reset(0); assert.equal(value, 0.5);
  await connection.dispose();
});
test('JSFX disposal releases subscriptions when its editor has already closed', async () => {
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  let stopped = false;
  const service = {
    on: async () => async () => { stopped = true; },
    invoke: async method => {
      if (method === 'endEdit') throw new Error('FX editor closed');
      return { count: 0, parameters: [], revision: 1, activation: 2, active: true };
    }
  };
  const connection = await connectJSFX({ host: { service: () => service } });
  await assert.rejects(connection.dispose(), /FX editor closed/);
  assert.equal(stopped, true);
});
test('cached pages discard stale responses and render fresh state before presentation', async () => {
  const { connectJSFX } = await import('../runtime/jsfx-webui.mjs');
  const events = {}, calls = [], order = [];
  let release;
  const state = (value, activation, revision) => ({ count: 1, activation, revision, active: true, parameters: [{ index: 0, value, normalized: value }] });
  const service = {
    on: async (name, fn) => { events[name] = fn; return async () => { delete events[name]; }; },
    invoke: async (method, data) => {
      calls.push({ method, data });
      if (method === 'getState') return state(.2, 1, 1);
      if (method === 'present') order.push('present');
      if (method === 'setParam') return new Promise(resolve => { release = () => resolve({ index: 0, value: .9, normalized: .9 }); });
      return true;
    }
  };
  const fx = await connectJSFX({ host: { service: () => service } });
  await new Promise(resolve => setTimeout(resolve, 5));
  fx.onChange(() => order.push('render'));
  const visible = []; fx.onVisibilityChange(value => visible.push(value));
  fx.setParam(0, .9);
  events.visibilityChanged({ active: false, activation: 2 });
  assert.equal(fx.active, false);
  fx.setParam(0, .8);
  order.length = 0;
  events.visibilityChanged({ active: true, activation: 3, state: state(.4, 3, 2) });
  assert.deepEqual(order, ['render', 'present']);
  release(); await new Promise(resolve => setImmediate(resolve));
  assert.equal(fx.state.parameters[0].value, .4);
  events.paramChanged({ activation: 1, revision: 100, parameters: [{ index: 0, value: .1 }] });
  assert.equal(fx.state.parameters[0].value, .4);
  assert.deepEqual(visible, [false, true]);
  assert.equal(calls.filter(c => c.method === 'setParam').length, 1);
  assert.equal(calls.find(c => c.method === 'setParam').data.activation, 1);
  await fx.dispose(); assert.deepEqual(events, {});
});
