const { test } = require('node:test');
const assert = require('node:assert/strict');
const sdk = import('../runtime/browser-files.mjs');

test('binary export preserves bytes, strips paths and handles cancellation before reading', async () => {
  const { saveBlob, MAX_DOWNLOAD_BYTES } = await sdk;
  const calls = [];
  const host = {
    dialog: { saveFile: async options => { calls.push(options); return '/chosen/entry.eqf'; } },
    fs: { writeBinary: async (...args) => calls.push(args) }
  };
  assert.equal(await saveBlob(new Blob([new Uint8Array([0, 255, 13, 10])]), '../entry.eqf', host), '/chosen/entry.eqf');
  assert.equal(calls[0].initialPath, 'entry.eqf');
  assert.deepEqual([...calls[1][1]], [0, 255, 13, 10]);
  assert.deepEqual(calls[1][2], { overwrite:true });
  host.dialog.saveFile = async () => null;
  const cancelled = new Blob(['x']);
  cancelled.arrayBuffer = () => { throw Error('must not read'); };
  assert.equal(await saveBlob(cancelled, 'x', host), null);
  await assert.rejects(saveBlob(new Blob([new Uint8Array(MAX_DOWNLOAD_BYTES + 1)]), 'x', host), { code:'BUFFER_LIMIT' });
});

test('download handler filters URLs, serializes dialogs, reports failures and detaches', async () => {
  const { installDownloadHandler } = await sdk;
  let listener, writes = [], errors = [], finish;
  const doc = { addEventListener: (_, fn) => listener = fn, removeEventListener: (_, fn) => { assert.equal(fn, listener); listener = null; } };
  const host = {
    dialog:{ saveFile: () => new Promise(resolve => finish = resolve) },
    fs:{ writeBinary: async (path, bytes) => writes.push([path, [...bytes]]) }
  };
  const dispose = installDownloadHandler({ document:doc, host, onError:e => errors.push(e) });
  function click(href, prevented = false) {
    const event = { defaultPrevented:prevented, target:{closest:() => ({href,download:'x.eqf'})}, preventDefault() { this.defaultPrevented = true; } };
    listener(event); return event;
  }
  assert.equal(click('https://example.com').defaultPrevented, false);
  assert.equal(click('file:///private').defaultPrevented, false);
  assert.equal(click('data:application/octet-stream;base64,AP8=').defaultPrevented, true);
  while (!finish) await new Promise(setImmediate);
  click('data:text/plain,ignored');
  finish('/chosen');
  while (!writes.length) await new Promise(setImmediate);
  await new Promise(setImmediate);
  assert.deepEqual(writes, [['/chosen', [0, 255]]]);
  host.dialog.saveFile = async () => { throw Error('dialog unavailable'); };
  click('data:text/plain,hello');
  while (!errors.length) await new Promise(setImmediate);
  assert.equal(errors[0].message, 'dialog unavailable');
  dispose(); assert.equal(listener, null);
});
