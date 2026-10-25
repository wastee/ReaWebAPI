#pragma once
#include <string>

namespace reaweb {
// Decide in the trusted key event, not from an asynchronously mirrored focus flag.
inline const char* daw_keyboard_script = R"JS(
(() => {
  if (window !== window.top) return;
  const post = window.chrome?.webview
    ? message => window.chrome.webview.postMessage(message)
    : message => window.webkit.messageHandlers.reaweb.postMessage(message);
  const roots = new WeakMap();
  const attach = Element.prototype.attachShadow;
  Object.defineProperty(Element.prototype, 'attachShadow', {
    ...Object.getOwnPropertyDescriptor(Element.prototype, 'attachShadow'),
    value: function(options) {
      const root = Reflect.apply(attach, this, [options]);
      roots.set(this, root);
      return root;
    }
  });
  let owner = null, composing = false, cancelled = false;
  const editable = event => {
    if (document.designMode === 'on') return true;
    const nodes = event.composedPath();
    let active = document.activeElement;
    while (active) {
      nodes.push(active);
      const root = roots.get(active) || active.shadowRoot;
      active = root?.activeElement;
    }
    return nodes.some(node => {
      const tag = node?.tagName;
      if (node?.isContentEditable || tag === 'IFRAME') return true;
      if (tag === 'TEXTAREA') return !node.disabled && !node.readOnly;
      if (tag !== 'INPUT' || node.disabled || node.readOnly) return false;
      return !['button', 'submit', 'reset', 'checkbox', 'radio', 'range', 'color', 'file', 'hidden'].includes(node.type);
    });
  };
  const send = key => post('__reawebDawKey:' + key);
  const release = () => {
    if (owner === 'host') { send('u'); cancelled = true; }
    owner = null;
    composing = false;
  };
  addEventListener('compositionstart', () => { composing = true; }, true);
  addEventListener('compositionend', () => { composing = false; }, true);
  addEventListener('keydown', event => {
    if (!event.isTrusted || (event.code !== 'Space' && event.key !== ' ')) return;
    if (cancelled && event.repeat) { event.preventDefault(); event.stopImmediatePropagation(); return; }
    cancelled = false;
    if (owner === null) owner = !event.repeat && !event.ctrlKey && !event.altKey && !event.metaKey && !event.shiftKey &&
      !composing && !event.isComposing && event.keyCode !== 229 && !editable(event) ? 'host' : 'page';
    if (owner !== 'host') return;
    event.preventDefault(); event.stopImmediatePropagation();
    send(event.repeat ? 'r' : 'd');
  }, true);
  addEventListener('keyup', event => {
    if (!event.isTrusted || (event.code !== 'Space' && event.key !== ' ')) return;
    if (owner === 'host' || cancelled) {
      event.preventDefault(); event.stopImmediatePropagation();
      if (owner === 'host') send('u');
    }
    owner = null; cancelled = false;
  }, true);
  addEventListener('blur', release);
  addEventListener('pagehide', release);
})();
)JS";
}
