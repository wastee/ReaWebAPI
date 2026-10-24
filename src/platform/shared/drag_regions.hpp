#pragma once
#include "core/core.hpp"
#include <cmath>

namespace reaweb {
// Layout is cached before the press. Native backends consume the actual mouse
// event, never a delayed JavaScript request to start a move or resize.
class DragRegions {
  Json regions_ = Json::array(), excluded_ = Json::array();
  double width_ = 0, height_ = 0;
  static bool contains(const Json& rectangles, double x, double y) {
    for (const auto& r : rectangles)
      if (x >= r[0].get<double>() && y >= r[1].get<double>() &&
          x < r[2].get<double>() && y < r[3].get<double>()) return true;
    return false;
  }
public:
  void clear() { regions_.clear(); excluded_.clear(); width_ = height_ = 0; }
  bool message(const std::string& text) {
    constexpr auto prefix = "{\"__reawebDragRegions\":";
    if (text.rfind(prefix, 0) != 0) return false;
    clear();
    if (text.size() > 131072) return true;
    try {
      const auto value = Json::parse(text).at("__reawebDragRegions");
      for (const auto* key : {"drag", "exclude"}) {
        const auto& rects = value.at(key);
        if (!rects.is_array() || rects.size() > 512) return true;
        for (const auto& r : rects) {
          if (!r.is_array() || r.size() != 4) return true;
          for (const auto& v : r) if (!v.is_number() || !std::isfinite(v.get<double>()) || std::abs(v.get<double>()) > 1000000) return true;
        }
      }
      const double width = value.at("width"), height = value.at("height");
      if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return true;
      regions_ = value.at("drag"); excluded_ = value.at("exclude"); width_ = width; height_ = height;
    } catch (const Json::exception&) {}
    return true;
  }
  bool hit(double x, double y, double width, double height) const {
    if (std::abs(width - width_) > 1 || std::abs(height - height_) > 1) return false;
    return !contains(excluded_, x, y) && contains(regions_, x, y);
  }
};

inline std::string drag_regions_script(const std::string& send, bool windows = false) {
  return R"JS(
;(() => {
  const interactive = '[data-reaweb-no-drag],button,a,input,select,textarea,summary,iframe,video,audio,[contenteditable]:not([contenteditable="false"]),[role="button"],[role="slider"],[role="textbox"],[tabindex]';
  let active = false, previous = '', last = 0;
  const nativeCSS = )JS" + std::string(windows ? "true" : "false") + R"JS(;
  const style = document.createElement('style');
  const css = '[data-reaweb-drag]{app-region:drag;-webkit-app-region:drag;user-select:none;-webkit-user-select:none}' +
    interactive + ',[data-reaweb-scroll]{app-region:no-drag;-webkit-app-region:no-drag;user-select:auto;-webkit-user-select:auto}';
  const update = value => { if (active !== !!value) previous = ''; active = !!value; if (nativeCSS) style.textContent = active ? css : ''; };
  Object.defineProperty(window, '__reawebDragRegionsActive', { value: update });
  const rects = element => {
    const s = getComputedStyle(element);
    if (s.visibility !== 'visible' || s.display === 'none' || s.pointerEvents === 'none') return [];
    let left = 0, top = 0, right = innerWidth, bottom = innerHeight;
    for (let p = element.parentElement; p; p = p.parentElement) {
      const c = getComputedStyle(p), r = p.getBoundingClientRect();
      if (c.overflowX !== 'visible') { left = Math.max(left, r.left); right = Math.min(right, r.right); }
      if (c.overflowY !== 'visible') { top = Math.max(top, r.top); bottom = Math.min(bottom, r.bottom); }
    }
    return Array.from(element.getClientRects(), r => [Math.max(left, r.left), Math.max(top, r.top), Math.min(right, r.right), Math.min(bottom, r.bottom)])
      .filter(r => r[0] < r[2] && r[1] < r[3]);
  };
  const scan = now => {
    requestAnimationFrame(scan);
    if (!active || now - last < 32 || !document.documentElement) return;
    last = now;
    if (!style.isConnected) { (document.head || document.documentElement).append(style); update(active); }
    const drag = [], exclude = [];
    for (const region of document.querySelectorAll('[data-reaweb-drag]')) {
      drag.push(...rects(region));
      for (const e of region.querySelectorAll('*')) {
        const c = getComputedStyle(e);
        const scroll = /(auto|scroll)/.test(c.overflowX + c.overflowY);
        if (nativeCSS && e.hasAttribute('data-reaweb-scroll') !== scroll) e.toggleAttribute('data-reaweb-scroll', scroll);
        if (scroll) exclude.push(...rects(e));
      }
    }
    for (const e of document.querySelectorAll(interactive)) exclude.push(...rects(e));
    // An oversized layout disables all cached drag regions instead of dropping exclusions.
    const value = {width:innerWidth, height:innerHeight, drag:drag.length <= 512 && exclude.length <= 512 ? drag : [], exclude:exclude.length <= 512 ? exclude : []};
    if (nativeCSS) return;
    const message = JSON.stringify({__reawebDragRegions:value});
    if (message !== previous) { previous = message; )JS" + send + R"JS( }
  };
  requestAnimationFrame(scan);
  reaper.events.on('windowstatechange', s => update(s.framelessActive));
  reaper.window.getState().then(s => update(s.framelessActive));
})();
)JS";
}
}
