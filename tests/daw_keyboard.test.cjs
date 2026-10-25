const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const script = fs.readFileSync(require('node:path').join(__dirname, '../src/platform/shared/daw_keyboard.hpp'), 'utf8').match(/R"JS\(([\s\S]*?)\)JS"/)[1];
function fixture() {
  const events = {}, sent = [];
  class Element { attachShadow() { return {activeElement: null}; } }
  const document = {activeElement: null, designMode: 'off'};
  const window = {chrome: {webview: {postMessage: value => sent.push(value.slice(15))}}};
  window.top = window;
  vm.runInNewContext(script, {window, document, Element, addEventListener: (name, fn) => { events[name] = fn; }});
  function key(type, extra = {}) {
    const event = {isTrusted: true, code: 'Space', key: ' ', repeat: false, composedPath: () => [],
      preventDefault() { this.prevented = true; }, stopImmediatePropagation() { this.stopped = true; }, ...extra};
    events[type](event);
    return event;
  }
  return {events, sent, document, key, Element};
}
test('host owns the entire press, including repeats and modifiers changed before release', () => {
  const f = fixture();
  assert.ok(f.key('keydown').prevented);
  f.document.activeElement = {tagName: 'INPUT', type: 'text'};
  assert.ok(f.key('keydown', {repeat: true, ctrlKey: true}).prevented);
  assert.ok(f.key('keyup', {ctrlKey: true}).prevented);
  assert.deepEqual(f.sent, ['d', 'r', 'u']);
});
test('editable, nested, Shadow DOM and IME presses remain in the page', () => {
  const cases = [
    {tagName: 'INPUT', type: 'text'}, {tagName: 'INPUT', type: 'number'}, {tagName: 'TEXTAREA'},
    {isContentEditable: true}, {tagName: 'IFRAME'},
    {shadowRoot: {activeElement: {shadowRoot: {activeElement: {isContentEditable: true}}}}}
  ];
  for (const active of cases) {
    const f = fixture(); f.document.activeElement = active;
    assert.ok(!f.key('keydown').prevented);
    f.document.activeElement = null;
    assert.ok(!f.key('keydown', {repeat: true}).prevented);
    assert.ok(!f.key('keyup').prevented);
    assert.deepEqual(f.sent, []);
  }
  const f = fixture(), host = new f.Element();
  host.attachShadow({mode: 'closed'}).activeElement = {tagName: 'INPUT', type: 'text'};
  f.document.activeElement = host;
  assert.ok(!f.key('keydown').prevented); f.key('keyup');
  f.document.activeElement = null;
  assert.ok(!f.key('keydown', {composedPath: () => [{isContentEditable: true}]}).prevented); f.key('keyup');
  f.events.compositionstart();
  assert.ok(!f.key('keydown').prevented); f.key('keyup');
  f.events.compositionend();
  assert.ok(!f.key('keydown', {isComposing: true}).prevented); f.key('keyup');
  assert.ok(!f.key('keydown', {keyCode: 229}).prevented); f.key('keyup');
  assert.deepEqual(f.sent, []);
});
test('controls route Space, other keys, modified Space and untrusted events do not', () => {
  for (const type of ['range', 'button', 'checkbox', 'radio']) {
    const f = fixture(); f.document.activeElement = {tagName: 'INPUT', type};
    assert.ok(f.key('keydown').prevented); f.key('keyup');
    assert.deepEqual(f.sent, ['d', 'u']);
  }
  for (const extra of [{ctrlKey:true}, {shiftKey:true}, {altKey:true}, {metaKey:true}, {isTrusted:false},
    {code:'Enter',key:'Enter'}, {code:'KeyA',key:'a'}, {repeat:true}]) {
    const f = fixture(); assert.ok(!f.key('keydown', extra).prevented); f.key('keyup', extra);
    assert.deepEqual(f.sent, []);
  }
});
test('blur releases host once, suppresses held repeats and trailing keyup, then permits a new press', () => {
  const f = fixture(); f.key('keydown'); f.events.blur(); f.events.pagehide();
  assert.ok(f.key('keydown', {repeat:true}).prevented);
  assert.ok(f.key('keyup').prevented);
  assert.deepEqual(f.sent, ['d','u']);
  f.key('keydown'); f.key('keyup');
  assert.deepEqual(f.sent, ['d','u','d','u']);
});
