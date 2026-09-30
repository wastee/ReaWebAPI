#pragma once
#include <charconv>
#include <functional>
#include <string>
#include <string_view>

namespace reaweb {
// Private platform message, consumed before the runtime bridge.
inline bool page_background_message(const std::string& message, const std::function<void(unsigned)>& apply) {
  constexpr std::string_view prefix = "__reawebBackground:";
  if (message.compare(0, prefix.size(), prefix) != 0) return false;
  unsigned color = 0;
  const auto end = message.data() + message.size();
  const auto parsed = std::from_chars(message.data() + prefix.size(), end, color);
  if (parsed.ec == std::errc() && parsed.ptr == end && color <= 0xffffff) apply(color);
  return true;
}

inline std::string page_background_script(const std::string& post) {
  return R"JS(
;(() => {
  const post = message => { )JS" + post + R"JS( };
  let previous, pending = false, body;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 1;
  const context = canvas.getContext('2d', {willReadFrequently: true});
  const rgba = color => {
    context.clearRect(0, 0, 1, 1);
    context.fillStyle = color;
    context.fillRect(0, 0, 1, 1);
    return context.getImageData(0, 0, 1, 1).data;
  };
  const update = () => {
    pending = false;
    if (!context || !document.documentElement) return;
    const root = getComputedStyle(document.documentElement);
    let color = rgba(root.backgroundColor);
    if (color[3] === 0 && root.backgroundImage === 'none' && document.body)
      color = rgba(getComputedStyle(document.body).backgroundColor);
    // Changing the backing color under translucent CSS would composite it twice.
    const value = color[3] === 255 ? (color[0] << 16) | (color[1] << 8) | color[2] : 0xffffff;
    if (value !== previous) { previous = value; post('__reawebBackground:' + value); }
  };
  const schedule = () => {
    if (!pending) { pending = true; requestAnimationFrame(update); }
  };
  const bodyObserver = new MutationObserver(schedule);
  const bindBody = () => {
    if (body !== document.body) {
      bodyObserver.disconnect();
      body = document.body;
      if (body) bodyObserver.observe(body, {attributes: true});
    }
    schedule();
  };
  const start = () => {
    new MutationObserver(bindBody).observe(document.documentElement, {attributes: true, childList: true});
    if (document.head) new MutationObserver(schedule).observe(document.head,
      {attributes: true, childList: true, characterData: true, subtree: true});
    bindBody();
    update();
  };
  if (document.readyState === 'loading') addEventListener('DOMContentLoaded', start, {once: true});
  else start();
  addEventListener('load', schedule, true);
  addEventListener('pageshow', schedule);
  addEventListener('resize', schedule);
  for (const event of ['transitionend', 'animationend']) addEventListener(event, event => {
    if (event.target === document.documentElement || event.target === document.body) schedule();
  }, true);
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', schedule);
})();
)JS";
}
}
