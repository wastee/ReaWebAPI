#include "platform/shared/jsfx_editor.hpp"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#else
#include <swell/swell.h>
#include "platform/shared/swell_window.hpp"
#endif
#include <reaper_plugin.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <map>
#include <set>
#include <thread>

namespace reaweb {
namespace {
using Clock = std::chrono::steady_clock;
bool host_meter_control(HWND editor, HWND child) {
  return child == GetDlgItem(editor, 1089) || child == GetDlgItem(editor, 1091);
}
RECT editor_content(HWND editor) {
  RECT content{}, label{}; GetClientRect(editor, &content);
  if (auto title = GetDlgItem(editor, 1023)) {
    GetWindowRect(title, &label);
    POINT origin{label.left, label.top}; ScreenToClient(editor, &origin);
    LONG bottom = 0;
    for (auto child = GetWindow(editor, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
      if (!(GetWindowLong(child, GWL_STYLE) & WS_VISIBLE)) continue;
      RECT rect{}; GetWindowRect(child, &rect);
      POINT first{rect.left, rect.top}, last{rect.right, rect.bottom};
      ScreenToClient(editor, &first); ScreenToClient(editor, &last);
      const auto edge = std::max(first.y, last.y);
      if (edge > 0 && edge <= origin.y) bottom = std::max(bottom, edge);
    }
    // Match the host FX toolbar's native content inset, including its UI scale.
#ifdef _WIN32
    RECT gap{0, 0, 0, 2}; MapDialogRect(editor, &gap);
    const auto inset = gap.bottom;
#elif defined(__APPLE__)
    const LONG inset = 2;
#else
    const LONG inset = std::max(1, (SWELL_GetScaling256() + 128) / 256);
#endif
    content.top = std::clamp(bottom > 0 ? bottom+inset : origin.y, LONG(0), content.bottom);
  }
  for (const int id : {1089, 1091}) {
    auto meter = GetDlgItem(editor, id);
    if (!meter) continue;
    RECT rect{}; GetWindowRect(meter, &rect);
    POINT first{rect.left, rect.top}, last{rect.right, rect.bottom};
    ScreenToClient(editor, &first); ScreenToClient(editor, &last);
    // no_meter hides these controls but retains their native bottom inset.
    const auto bottom = std::max(first.y, last.y);
    if (bottom > content.top && bottom <= content.bottom) content.bottom = bottom;
    if (!(GetWindowLong(meter, GWL_STYLE) & WS_VISIBLE)) continue;
    if (id == 1089) content.left = std::clamp(std::max(first.x, last.x), content.left, content.right);
    else content.right = std::clamp(std::min(first.x, last.x), content.left, content.right);
  }
  return content;
}
bool host_toolbar_control(HWND editor, HWND child, LONG content_top) {
  RECT rect{}; GetWindowRect(child, &rect);
  POINT first{rect.left, rect.top}, last{rect.right, rect.bottom};
  ScreenToClient(editor, &first); ScreenToClient(editor, &last);
  // Native knobs may extend slightly above the editor's client rectangle.
  const auto bottom = std::max(first.y, last.y);
  return bottom > 0 && bottom <= content_top;
}
void resize_editor_owner(HWND owner, int width, int height, bool preserve_upper_edge = true) {
#ifdef __APPLE__
  if (preserve_upper_edge) {
    RECT rect{}; GetWindowRect(owner, &rect);
    SetWindowPos(owner, nullptr, rect.left, rect.bottom - height, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
#endif
  (void)preserve_upper_edge;
  SetWindowPos(owner, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
struct ChainLayout {
  HWND owner = nullptr;
  HWND size_owner = nullptr;
  HWND backdrop = nullptr;
  JsfxTarget target;
  int active_fx = -1;
  std::weak_ptr<JsfxEditorHost> active_host;
  std::function<int()> selected;
  std::function<void()> on_destroy;
  RECT margins{};
  unsigned background = 0xffffff;
  int width = 0, height = 0;
#ifdef __APPLE__
  POINT upper_left{};
#endif
  bool alive = true, adjusting = false, user_sizing = false, frame_only = false;
  Clock::time_point handoff_until{};
  std::function<bool()> applies;
#ifdef _WIN32
  bool initial_cloak = false;
#else
  WNDPROC original = nullptr;
  std::unique_ptr<SwellWindow> backdrop_window;
#endif
  ~ChainLayout();
  void stop();
  void reveal() {
#ifdef _WIN32
    if (!initial_cloak || !alive || !IsWindow(owner)) return;
    RedrawWindow(owner, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
    DwmFlush();
    const BOOL cloak = FALSE;
    DwmSetWindowAttribute(owner, DWMWA_CLOAK, &cloak, sizeof(cloak));
    initial_cloak = false;
#endif
  }
  void cover(HWND editor, std::optional<JsfxEditorDiscovery::Size> size = {});
  void request_size(int requested_width, int requested_height) {
    if (!alive || adjusting || !IsWindow(owner) || !IsWindow(size_owner) ||
        (size_owner != owner && !IsChild(size_owner, owner)) || (GetWindowLong(size_owner, GWL_STYLE) & WS_CHILD)) return;
    RECT client{}, outer{}; GetClientRect(owner, &client); GetWindowRect(size_owner, &outer);
    const int dx = std::max(0, requested_width - int(client.right-margins.left-margins.right));
    const int dy = std::max(0, requested_height - int(client.bottom-margins.top-margins.bottom));
    adjusting = true;
    if (dx || dy) resize_editor_owner(size_owner, std::abs(int(outer.right-outer.left))+dx,
      std::abs(int(outer.bottom-outer.top))+dy);
    remember(); adjusting = false;
  }
  void resize_children() {
    if (!backdrop || !IsWindow(backdrop)) return;
    RECT r{}; GetClientRect(backdrop, &r);
    for (auto child = GetWindow(backdrop, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
      SetWindowPos(child, nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  void resize_viewport() {
    if (!backdrop || !IsWindow(backdrop)) return;
    RECT r{}; GetClientRect(owner, &r);
    SetWindowPos(backdrop, HWND_TOP, margins.left, margins.top,
      std::max(1, int(r.right-margins.left-margins.right)), std::max(1, int(r.bottom-margins.top-margins.bottom)), SWP_NOACTIVATE);
    resize_children();
  }
  void set_background(unsigned color) {
    if (background == color) return;
    background = color;
#ifndef _WIN32
    if (backdrop_window) backdrop_window->set_background(color);
#endif
    if (backdrop && IsWindow(backdrop)) InvalidateRect(backdrop, nullptr, FALSE);
  }
  void remember() {
    RECT r{}; GetWindowRect(size_owner, &r);
    width = std::abs(int(r.right - r.left)); height = std::abs(int(r.bottom - r.top));
#ifdef __APPLE__
    upper_left = {r.left, r.bottom};
#endif
  }
  void handoff() {
    if (!alive) return;
    remember(); handoff_until = Clock::now() + std::chrono::seconds(1);
  }
  bool holding() const {
    if (!alive || adjusting || user_sizing || Clock::now() >= handoff_until ||
        (GetWindowLong(size_owner, GWL_STYLE) & WS_CHILD)) return false;
    try { return applies(); } catch (...) { return false; }
  }
};
bool native_editor_ready(const ChainLayout& layout);
bool retained_editor_ready(const ChainLayout& layout);
void restore_chain(const std::shared_ptr<ChainLayout>& layout);
std::map<HWND, std::shared_ptr<ChainLayout>> chain_layouts;
#ifndef _WIN32
std::function<void(HWND)> detach_editor_probe;
#endif
#ifdef _WIN32
LRESULT CALLBACK backdrop_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
  if (message == WM_SIZE) reinterpret_cast<ChainLayout*>(data)->resize_children();
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_PAINT) {
    const auto color = reinterpret_cast<ChainLayout*>(data)->background;
    PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint);
    if (dc) {
      auto brush = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
      FillRect(dc, &paint.rcPaint, brush); DeleteObject(brush); EndPaint(window, &paint);
    }
    return 0;
  }
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, backdrop_proc, 1);
  return DefSubclassProc(window, message, wp, lp);
}
#endif
void ChainLayout::cover(HWND editor, std::optional<JsfxEditorDiscovery::Size> size) {
  if (!alive || adjusting) return;
  if (size && !backdrop && size_owner != owner) {
    // A newly shown container may still carry its previous child layout.
    RECT client{}; GetClientRect(size_owner, &client);
    adjusting = true;
    SendMessage(size_owner, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom));
    adjusting = false;
  }
  {
    RECT rect{}, parent{}; GetWindowRect(editor, &rect); GetClientRect(owner, &parent);
    POINT origin{rect.left, rect.top}; ScreenToClient(owner, &origin);
    const auto content = editor_content(editor);
    margins = {origin.x+content.left, origin.y+content.top, parent.right-origin.x-content.right,
      parent.bottom-origin.y-content.bottom};
  }
  // Returning from a floating editor can reuse a backdrop after native layout has shrunk.
  if (size) request_size(size->width, size->height);
  if (!backdrop) {
#ifdef _WIN32
    backdrop = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
      owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!backdrop) return;
    if (!SetWindowSubclass(backdrop, backdrop_proc, 1, reinterpret_cast<DWORD_PTR>(this))) {
      DestroyWindow(backdrop); backdrop = nullptr; return;
    }
#else
    backdrop_window = std::make_unique<SwellWindow>("", owner, std::function<void()>{}, std::function<void()>{}, true, true);
    backdrop_window->set_background(background);
    backdrop = static_cast<HWND>(backdrop_window->handle());
    backdrop_window->resize = [this] { resize_children(); };
#endif
  }
  resize_viewport();
  if (!(GetWindowLong(backdrop, GWL_STYLE) & WS_VISIBLE)) ShowWindow(backdrop, SW_SHOWNA);
}
#ifdef _WIN32
LRESULT CALLBACK chain_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
#else
LRESULT chain_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
#endif
  const auto it = chain_layouts.find(window);
#ifdef _WIN32
  if (it == chain_layouts.end()) return DefSubclassProc(window, message, wp, lp);
#else
  if (it == chain_layouts.end()) return DefWindowProc(window, message, wp, lp);
#endif
  auto state = it->second;
  const bool selection = message == WM_NOTIFY && lp && reinterpret_cast<NMHDR*>(lp)->code == LVN_ITEMCHANGED;
  if (selection && !state->adjusting) {
    try { if (state->applies()) state->handoff(); } catch (...) {}
  }
  std::vector<std::shared_ptr<ChainLayout>> sized;
  for (const auto& entry : chain_layouts)
    if (entry.second->alive && entry.second->size_owner == window) sized.push_back(entry.second);
  for (const auto& layout : sized) {
#ifdef _WIN32
    if (message == WM_ENTERSIZEMOVE) { layout->user_sizing = true; layout->handoff_until = {}; }
    if (message == WM_EXITSIZEMOVE) layout->user_sizing = false;
    if (message == WM_SYSCOMMAND || message == WM_DPICHANGED) layout->handoff_until = {};
    if (message == WM_WINDOWPOSCHANGING && layout->holding()) {
      auto& position = *reinterpret_cast<WINDOWPOS*>(lp);
      if (!(position.flags & SWP_NOSIZE)) { position.cx = layout->width; position.cy = layout->height; }
      break;
    }
#else
    // Observe the actual frame before SWELL lays out embedded container editors.
    if (message == WM_SIZE && layout->holding()) {
      RECT r{}; GetWindowRect(window, &r);
      if (std::abs(int(r.right-r.left)) != layout->width || std::abs(int(r.bottom-r.top)) != layout->height) {
        layout->adjusting = true;
#ifdef __APPLE__
        SetWindowPos(window, nullptr, layout->upper_left.x, layout->upper_left.y-layout->height,
          layout->width, layout->height, SWP_NOZORDER | SWP_NOACTIVATE);
#else
        resize_editor_owner(window, layout->width, layout->height);
#endif
        layout->adjusting = false;
        return 0;
      }
    }
#endif
  }
  if (message == WM_DESTROY) {
    state->alive = false;
    auto detach = std::move(state->on_destroy);
    if (detach) detach();
  }
#ifdef _WIN32
  const auto result = DefSubclassProc(window, message, wp, lp);
  if (message == WM_NCDESTROY) { RemoveWindowSubclass(window, chain_proc, 1); chain_layouts.erase(window); }
#else
  const auto result = state->original(window, message, wp, lp);
  if (message == WM_DESTROY) chain_layouts.erase(window);
#endif
  if (state->alive && message == WM_SIZE) {
    for (const auto& layout : sized) if (layout->alive) layout->remember();
    state->resize_viewport();
  }
  if (state->alive && state->backdrop && (message == WM_SIZE || message == WM_COMMAND || selection)) {
    try { if (!state->applies() && native_editor_ready(*state)) restore_chain(state); } catch (...) {}
  }
  return result;
}
ChainLayout::~ChainLayout() { stop(); }
void ChainLayout::stop() {
  if (!alive || !IsWindow(owner)) return;
  reveal();
  alive = false;
#ifdef _WIN32
  if (backdrop && IsWindow(backdrop)) DestroyWindow(backdrop);
#else
  backdrop_window.reset();
#endif
  backdrop = nullptr;
#ifdef _WIN32
  RemoveWindowSubclass(owner, chain_proc, 1);
#else
  if (GetWindowLong(owner, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(chain_proc))
    SetWindowLong(owner, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(original));
#endif
}
struct FocusedFx { JsfxTarget target; int fx = -1; };
int parent_container(Host& host, JsfxTarget target, int fx) {
  const auto value = target.config(host, fx, "parent_container");
  char* end = nullptr; const auto index = std::strtol(value.c_str(), &end, 10);
  return !value.empty() && end && !*end && index >= 0 && index <= INT_MAX ? int(index) : -1;
}
HWND chain_list(HWND window) {
  const auto list = GetDlgItem(window, 1076);
  return list && GetParent(list) == window ? list : nullptr;
}
struct ChainPosition { HWND root; int container, selected; };
std::optional<ChainPosition> chain_position(Host& host, JsfxTarget target, HWND owner, int hint, bool retain = false) {
  std::vector<HWND> path;
  for (auto window = owner; window; window = GetParent(window)) {
    if (chain_list(window)) path.push_back(window);
    if (path.size() > 64) return {};
    if (!(GetWindowLong(window, GWL_STYLE) & WS_CHILD)) break;
  }
  if (path.empty() || path.front() != owner) return {};
  std::reverse(path.begin(), path.end());
  int container = -1;
  for (int depth = 0, fx = hint; fx >= 0 && depth < 64; ++depth) {
    auto frame = static_cast<HWND>(target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), fx));
    if (frame && (frame == path.front() || IsChild(frame, path.front())) &&
        target.config(host, fx, "fx_type") == "Container") { container = fx; break; }
    const auto parent = parent_container(host, target, fx);
    if (parent == fx) return {};
    fx = parent;
  }
  int selected = -1;
  for (auto window : path) {
    if (window != path.front()) container = selected;
    if (container < 0) {
      if (window != path.front()) return {};
      selected = target.selected(host);
    } else {
      if (target.config(host, container, "fx_type") != "Container") return {};
      const auto row = ListView_GetNextItem(chain_list(window), -1, LVNI_SELECTED);
      const auto value = row >= 0 ? target.config(host, container, "container_item." + std::to_string(row)) : std::string();
      char* end = nullptr; const auto index = std::strtol(value.c_str(), &end, 10);
      selected = !value.empty() && end && !*end && index >= 0 && index <= INT_MAX ? int(index) : -1;
      if (row < 0 && retain) {
        // Clearing list selection does not replace the host's current editor.
        for (int depth = 0, child = hint; child >= 0 && depth < 64; ++depth) {
          const auto parent = parent_container(host, target, child);
          if (parent == container) { selected = child; break; }
          if (parent == child) break;
          child = parent;
        }
      }
    }
  }
  return ChainPosition{path.front(), container, selected};
}
std::optional<FocusedFx> focused_fx(Host& host, HWND owner) {
  const auto focus = GetFocus();
  if (!focus || (focus != owner && !IsChild(owner, focus))) return {};
  auto query = reinterpret_cast<bool (*)(int, int*, int*, int*, int*, int*)>(host.native_function("GetTouchedOrFocusedFX"));
  auto track_at = reinterpret_cast<void* (*)(void*, int)>(host.native_function("GetTrack"));
  auto master = reinterpret_cast<void* (*)(void*)>(host.native_function("GetMasterTrack"));
  auto item_at = reinterpret_cast<void* (*)(void*, int)>(host.native_function("GetTrackMediaItem"));
  auto take_at = reinterpret_cast<void* (*)(void*, int)>(host.native_function("GetTake"));
  if (!query || !track_at || !master || !item_at || !take_at) return {};
  int track = -1, item = -1, take = -1, fx = -1, flags = 0;
  if (!query(1, &track, &item, &take, &fx, &flags) || flags || fx < 0) return {};
  auto project = host.current_project();
  JsfxTarget target{track < 0 ? master(project) : track_at(project, track)};
  if (!host.valid_track(project, target.track)) return {};
  if (item >= 0) {
    auto media = item_at(target.track, item);
    if (!media || take < 0 || !(target.take = take_at(media, take))) return {};
  } else target.input = bool(fx & 0x1000000);
  const auto floating = target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), fx);
  if (floating != owner && target.selected(host) != fx) {
    bool verified = false;
    for (int depth = 0, parent = parent_container(host, target, fx); parent >= 0 && depth < 64; ++depth) {
      const auto frame = static_cast<HWND>(target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), parent));
      if (target.selected(host) == parent || (frame && (frame == owner || IsChild(frame, owner)))) { verified = true; break; }
      const auto next = parent_container(host, target, parent);
      if (next == parent) break;
      parent = next;
    }
    if (!verified) return {};
  }
  return FocusedFx{target, fx};
}
std::optional<FocusedFx> known_floating_fx(Host& host, HWND owner) {
  // Floating windows can become visible before focus leaves their previous chain.
  for (const auto& entry : chain_layouts) {
    const auto& layout = *entry.second;
    if (!layout.alive || layout.active_fx < 0 || !host.valid_track(host.current_project(), layout.target.track)) continue;
    const auto target = layout.target;
    if (target.take) {
      auto valid = reinterpret_cast<bool (*)(void*, void*, const char*)>(host.native_function("ValidatePtr2"));
      if (!valid || !valid(host.current_project(), target.take, "MediaItem_Take*")) continue;
    }
    for (int depth = 0, fx = layout.active_fx; fx >= 0 && depth < 64; ++depth) {
      if (target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), fx) == owner)
        return FocusedFx{target, fx};
      const auto parent = parent_container(host, target, fx);
      if (parent == fx) break;
      fx = parent;
    }
  }
  return {};
}
std::optional<FocusedFx> focused_chain_fx(Host& host, HWND owner) {
  if (const auto focused = focused_fx(host, owner)) return focused;
  // A floating container frame can own focus outside its embedded chain.
  for (auto window = owner; GetWindowLong(window, GWL_STYLE) & WS_CHILD;) {
    window = GetParent(window);
    if (!window) break;
    if (const auto focused = focused_fx(host, window)) return focused;
  }
  return {};
}
std::shared_ptr<ChainLayout> chain_layout(HWND owner, Host& host, JsfxTarget target, int fx, JsfxEditorDiscovery::Match match) {
  auto it = chain_layouts.find(owner);
  if (it != chain_layouts.end() && it->second->target != target) {
    auto previous = it->second;
    auto detach = std::move(previous->on_destroy);
    if (detach) detach();
    restore_chain(previous); previous->stop(); chain_layouts.erase(it); it = chain_layouts.end();
  }
  if (it != chain_layouts.end() && !it->second->frame_only) return it->second;
  const bool observing = it != chain_layouts.end();
  auto state = observing ? it->second : std::make_shared<ChainLayout>();
  state->frame_only = false;
  state->owner = state->size_owner = owner; state->target = target; state->remember();
  auto project = host.current_project();
  const auto generation = host.project_generation ? host.project_generation() : 0;
  const auto track_guid = host.track_guid(target.track);
  const auto position = chain_position(host, target, owner, fx);
  if (!position) return {};
  state->size_owner = position->root;
  int container_fx = position->container;
  const bool container_chain = container_fx >= 0;
  Guid container_guid{};
  if (container_fx >= 0) {
    auto guid = target.api<Guid* (*)(void*, int)>(host, "GetFXGUID")(target.object(), container_fx);
    if (!guid) return {};
    container_guid = *guid;
    for (int depth = 0, parent = container_fx; parent >= 0 && depth < 64; ++depth) {
      const auto frame = static_cast<HWND>(target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), parent));
      if (frame && (frame == owner || IsChild(frame, owner))) { state->size_owner = frame; break; }
      const auto next = parent_container(host, target, parent);
      if (next == parent) break;
      parent = next;
    }
  }
  Guid take_guid{}, item_guid{}; void* item = nullptr;
  auto info = reinterpret_cast<void* (*)(void*, const char*, void*)>(host.native_function("GetSetMediaItemTakeInfo"));
  auto item_info = reinterpret_cast<void* (*)(void*, const char*, void*)>(host.native_function("GetSetMediaItemInfo"));
  auto take_item = reinterpret_cast<void* (*)(void*)>(host.native_function("GetMediaItemTake_Item"));
  auto item_track = reinterpret_cast<void* (*)(void*)>(host.native_function("GetMediaItem_Track"));
  auto valid = reinterpret_cast<bool (*)(void*, void*, const char*)>(host.native_function("ValidatePtr2"));
  if (target.take) {
    if (!info || !item_info || !take_item || !item_track || !valid || !valid(project, target.take, "MediaItem_Take*")) return {};
    item = take_item(target.take);
    auto tg = info(target.take, "GUID", nullptr), ig = item_info(item, "GUID", nullptr);
    if (!tg || !ig) return {};
    take_guid = *static_cast<Guid*>(tg); item_guid = *static_cast<Guid*>(ig);
  }
  const auto fx_guid = target.api<Guid* (*)(void*, int)>(host, "GetFXGUID");
  auto initial_guid = fx_guid(target.object(), fx);
  if (!initial_guid) return {};
  state->selected = [&host, project, generation, target, track_guid, take_guid, item_guid, item, info, item_info, take_item, item_track, valid, owner, container_chain, container_fx, container_guid,
                     weak = std::weak_ptr<ChainLayout>(state), retained_fx = fx, retained_guid = *initial_guid, fx_guid]() mutable {
    if (host.current_project() != project || (host.project_generation && host.project_generation() != generation) ||
        !host.valid_track(project, target.track) || host.track_guid(target.track) != track_guid) return -1;
    if (target.take) {
      if (!valid(project, target.take, "MediaItem_Take*") || !valid(project, item, "MediaItem*") ||
          take_item(target.take) != item || item_track(item) != target.track) return -1;
      auto tg = info(target.take, "GUID", nullptr), ig = item_info(item, "GUID", nullptr);
      if (!tg || !ig || *static_cast<Guid*>(tg) != take_guid || *static_cast<Guid*>(ig) != item_guid) return -1;
    }
    const auto focused = focused_fx(host, owner);
    if (focused && focused->target != target) return -1;
    if (container_chain) {
      const auto guid = target.api<Guid* (*)(void*, int)>(host, "GetFXGUID");
      const auto matches = [&](int index) { auto g = guid(target.object(), index); return g && *g == container_guid; };
      if (container_fx < 0 || !matches(container_fx)) {
        container_fx = -1;
        for (const auto index : target.indices(host)) if (matches(index)) { container_fx = index; break; }
        if (container_fx < 0) return -1;
      }
      auto layout = weak.lock();
      bool retain = layout && retained_editor_ready(*layout);
      if (retain) {
        auto current = fx_guid(target.object(), retained_fx);
        if (!current || *current != retained_guid) {
          retained_fx = -1;
          for (const auto index : target.indices(host)) {
            auto candidate = fx_guid(target.object(), index);
            if (candidate && *candidate == retained_guid) { retained_fx = index; break; }
          }
        }
        retain = retained_fx >= 0;
      }
      const auto position = chain_position(host, target, owner, retain ? retained_fx : container_fx, retain);
      const auto selected = position && position->container == container_fx ? position->selected : -1;
      if (selected >= 0) if (auto current = fx_guid(target.object(), selected)) {
        retained_fx = selected; retained_guid = *current;
      }
      return selected;
    }
    return target.selected(host);
  };
  state->selected = [selected = std::move(state->selected), &host, target] {
    const int fx = selected();
    return fx >= 0 && target.api<void* (*)(void*, int)>(host, "GetFloatingWindow")(target.object(), fx) ? -1 : fx;
  };
  state->applies = [selected = state->selected, target, match = std::move(match)] {
    const auto fx = selected();
    return fx >= 0 && match(target, fx);
  };
  if (!observing) {
#ifdef _WIN32
    if (!SetWindowSubclass(owner, chain_proc, 1, 0)) return {};
#else
    if (detach_editor_probe) detach_editor_probe(owner);
    state->original = reinterpret_cast<WNDPROC>(GetWindowLong(owner, GWL_WNDPROC));
    if (!state->original) return {};
    SetWindowLong(owner, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(chain_proc));
#endif
    chain_layouts.emplace(owner, state);
  }
  if (state->size_owner != owner && !chain_layouts.count(state->size_owner)) {
    auto frame = std::make_shared<ChainLayout>();
    frame->owner = frame->size_owner = state->size_owner; frame->target = target; frame->frame_only = true;
    frame->selected = [] { return -1; }; frame->applies = [] { return false; };
#ifdef _WIN32
    if (SetWindowSubclass(frame->owner, chain_proc, 1, 0)) chain_layouts.emplace(frame->owner, frame);
#else
    if (detach_editor_probe) detach_editor_probe(frame->owner);
    frame->original = reinterpret_cast<WNDPROC>(GetWindowLong(frame->owner, GWL_WNDPROC));
    if (frame->original) {
      SetWindowLong(frame->owner, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(chain_proc));
      chain_layouts.emplace(frame->owner, frame);
    }
#endif
  }
  return state;
}
struct Attachment {
  HWND editor = nullptr, child = nullptr, owner = nullptr;
  std::vector<HWND> hidden;
  bool alive = true, prepared = false;
  unsigned background = 0xffffff;
  std::function<void()> on_destroy;
  std::shared_ptr<ChainLayout> layout;
#ifdef _WIN32
  bool initial_cloak = false;
#else
  WNDPROC original = nullptr;
#endif
  void reveal() {
#ifdef _WIN32
    if (!initial_cloak || !alive || !IsWindow(owner)) return;
    RedrawWindow(owner, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
    DwmFlush();
    const BOOL cloak = FALSE;
    DwmSetWindowAttribute(owner, DWMWA_CLOAK, &cloak, sizeof(cloak));
    initial_cloak = false;
#endif
  }
  void resize() {
    if (alive && prepared && layout && IsWindow(editor)) layout->cover(editor);
    if (!alive || !prepared || !child || !IsWindow(child) || GetParent(child) != editor) return;
    const auto r = editor_content(editor);
    SetWindowPos(child, HWND_TOP, r.left, r.top, r.right-r.left, r.bottom-r.top, SWP_NOACTIVATE);
  }
  void conceal() {
    if (!alive || !prepared) return;
    const auto content_top = editor_content(editor).top;
    for (auto c = GetWindow(editor, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
      if (c != child && !host_meter_control(editor, c) && !host_toolbar_control(editor, c, content_top) &&
          (child || GetWindowLong(c, GWL_ID) != 0) && (GetWindowLong(c, GWL_STYLE) & WS_VISIBLE)) {
        if (std::find(hidden.begin(), hidden.end(), c) == hidden.end()) hidden.push_back(c);
        ShowWindow(c, SW_HIDE);
      }
  }
};
std::map<HWND, std::shared_ptr<Attachment>> attachments;
bool retained_editor_ready(const ChainLayout& layout) {
  for (const auto& entry : attachments) {
    const auto& editor = *entry.second;
    if (editor.layout.get() == &layout && editor.alive && editor.prepared &&
        IsWindowVisible(editor.editor) && IsChild(layout.owner, editor.editor)) return true;
  }
  return false;
}
bool native_editor_ready(const ChainLayout& layout) {
  for (auto child = GetWindow(layout.owner, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
    if (child == layout.backdrop || !IsWindowVisible(child)) continue;
    const auto id = GetWindowLong(child, GWL_ID);
    if (id != 0 && id != 1077) continue;
    bool previous = false;
    for (const auto& entry : attachments) {
      const auto& editor = *entry.second;
      if (editor.layout.get() == &layout && editor.alive && editor.prepared &&
          (editor.editor == child || IsChild(child, editor.editor))) { previous = true; break; }
    }
    if (!previous) return true;
  }
  return false;
}
void restore_native(Attachment& state) {
  const bool prepared = state.prepared;
  state.child = nullptr; state.prepared = false;
  if (state.alive && IsWindow(state.editor)) {
    for (auto child : state.hidden) if (IsWindow(child) && GetParent(child) == state.editor) ShowWindow(child, SW_SHOWNA);
    if (prepared) {
      RECT rect{}; GetClientRect(state.editor, &rect);
      SendMessage(state.editor, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
    }
    InvalidateRect(state.editor, nullptr, TRUE);
  }
  state.hidden.clear();
  state.reveal();
}
void restore_chain(const std::shared_ptr<ChainLayout>& layout) {
  if (layout->alive && layout->backdrop) ShowWindow(layout->backdrop, SW_HIDE);
  for (const auto& entry : attachments) if (entry.second->layout == layout) restore_native(*entry.second);
  layout->reveal();
}
#ifdef _WIN32
LRESULT CALLBACK editor_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
#else
LRESULT editor_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
#endif
  const auto it = attachments.find(window);
#ifdef _WIN32
  if (it == attachments.end()) return DefSubclassProc(window, message, wp, lp);
#else
  if (it == attachments.end()) return DefWindowProc(window, message, wp, lp);
#endif
  auto state = it->second;
  if (state->prepared && message == WM_COMMAND && HIWORD(wp) == EN_KILLFOCUS) {
    const auto control = GetDlgItem(window, LOWORD(wp));
    // JSFX commits edit fields on focus loss, even when the value is unchanged.
    if (control && std::find(state->hidden.begin(), state->hidden.end(), control) != state->hidden.end())
      return 0;
  }
  if (state->prepared && state->layout && (message == WM_DESTROY || (message == WM_SHOWWINDOW && !wp)))
    state->layout->handoff();
  if (state->prepared && message == WM_SHOWWINDOW && wp) state->conceal();
  const auto fill_background = [&](HDC dc, const RECT& rect) {
    if (!dc) return;
    const auto color = state->background;
    auto brush = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
    FillRect(dc, &rect, brush); DeleteObject(brush);
  };
  const auto fill_native_frame = [&](HDC dc) {
    if (!dc) return;
    const auto content = editor_content(window);
#ifdef _WIN32
    // Keep the host's themed background, excluding the WebUI content area.
    const auto saved = SaveDC(dc);
    if (!saved) return;
    ExcludeClipRect(dc, content.left, content.top, content.right, content.bottom);
    DefSubclassProc(window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
    RestoreDC(dc, saved);
#else
    RECT client{}; GetClientRect(window, &client);
    auto brush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    for (const auto& rect : {RECT{0, 0, client.right, content.top},
          RECT{0, content.top, content.left, client.bottom},
          RECT{content.right, content.top, client.right, client.bottom},
          RECT{content.left, content.bottom, content.right, client.bottom}})
      FillRect(dc, &rect, brush);
    DeleteObject(brush);
#endif
  };
  if (state->prepared && message == WM_ERASEBKGND) {
    // The host owns the preset/parameter/routing toolbar above the JSFX content.
#ifndef _WIN32
    state->original(window, message, wp, lp);
#endif
    fill_native_frame(reinterpret_cast<HDC>(wp));
    if (!state->child || !IsWindowVisible(state->child)) {
      fill_background(reinterpret_cast<HDC>(wp), editor_content(window));
    }
    return 1;
  }
  if (state->prepared && message == WM_PAINT) {
    state->conceal();
    PAINTSTRUCT paint{};
    if (auto dc = BeginPaint(window, &paint)) {
      fill_native_frame(dc);
      if (!state->child || !IsWindowVisible(state->child)) {
        auto rect = paint.rcPaint;
        const auto content = editor_content(window);
        rect.top = std::max(rect.top, content.top);
        rect.left = std::max(rect.left, content.left);
        rect.right = std::min(rect.right, content.right);
        rect.bottom = std::min(rect.bottom, content.bottom);
        if (rect.bottom > rect.top) fill_background(dc, rect);
      }
      EndPaint(window, &paint);
    }
    return 0;
  }
  if (message == WM_DESTROY) {
    state->alive = false;
    // Detach the hosted child before the native editor destroys its descendants.
    auto detach = std::move(state->on_destroy);
    if (detach) detach();
    state->child = nullptr;
  }
#ifdef _WIN32
  const auto result = DefSubclassProc(window, message, wp, lp);
  if (message == WM_NCDESTROY) { RemoveWindowSubclass(window, editor_proc, 1); attachments.erase(window); }
#else
  const auto result = state->original(window, message, wp, lp);
  if (message == WM_DESTROY) attachments.erase(window);
#endif
  if (message == WM_SIZE
#ifdef _WIN32
      || message == WM_WINDOWPOSCHANGED
#endif
      ) {
    // Native layout can show controls again after the initial concealment.
    state->conceal(); state->resize();
  }
  if (message == WM_SHOWWINDOW && wp) state->conceal();
  return result;
}
class EditorHost final : public JsfxEditorHost {
  std::shared_ptr<Attachment> state_;
  bool observing_ = false;
  bool revealed_ = false;
  int requested_width_ = 0, requested_height_ = 0;
  bool native_valid() const {
    return state_->alive && IsWindow(state_->editor) && IsWindow(state_->owner) &&
      (state_->owner == state_->editor || IsChild(state_->owner, state_->editor)) &&
      GetDlgItem(state_->editor, 1023) && GetDlgItem(state_->editor, 1062);
  }
public:
  EditorHost(HWND editor, HWND owner, std::shared_ptr<ChainLayout> layout) : state_(std::make_shared<Attachment>()) {
    state_->editor = editor; state_->owner = owner;
    state_->layout = std::move(layout);
    if (state_->layout) state_->background = state_->layout->background;
    if (attachments.count(editor)) throw Error("EDITOR_BUSY", "JSFX editor already attached");
#ifdef _WIN32
    if (!SetWindowSubclass(editor, editor_proc, 1, 0)) throw Error("EDITOR_UNAVAILABLE", "Cannot observe JSFX editor lifetime");
#else
    if (detach_editor_probe) detach_editor_probe(editor);
    state_->original = reinterpret_cast<WNDPROC>(GetWindowLong(editor, GWL_WNDPROC));
    if (!state_->original) throw Error("EDITOR_UNAVAILABLE", "Cannot observe JSFX editor lifetime");
    SetWindowLong(editor, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(editor_proc));
#endif
    attachments.emplace(editor, state_);
  }
  ~EditorHost() override {
    if (observing_) return;
    restore();
    const auto editor = state_->editor;
    if (!state_->alive || !IsWindow(editor)) return;
#ifdef _WIN32
    RemoveWindowSubclass(editor, editor_proc, 1); attachments.erase(editor);
#else
    if (GetWindowLong(editor, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(editor_proc)) {
      SetWindowLong(editor, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(state_->original)); attachments.erase(editor);
    }
#endif
  }
  void* parent() const override { return state_->editor; }
  void on_destroy(std::function<void()> callback) override { state_->on_destroy = std::move(callback); }
  bool valid() const override {
    return native_valid() && IsWindowVisible(state_->editor);
  }
  void resize(void* child) override { state_->child = static_cast<HWND>(child); state_->resize(); }
  void prepare() override {
#ifdef _WIN32
    if (!state_->prepared && !state_->layout && !chain_list(state_->owner) &&
        !(GetWindowLong(state_->owner, GWL_STYLE) & WS_CHILD)) {
      const BOOL cloak = TRUE;
      state_->initial_cloak = SUCCEEDED(DwmSetWindowAttribute(state_->owner, DWMWA_CLOAK, &cloak, sizeof(cloak)));
    }
#endif
    state_->prepared = true; state_->conceal();
  }
  void reveal() override {
    if (!revealed_) request_size(requested_width_, requested_height_);
    state_->reveal(); revealed_ = true;
  }
  void park() override { if (state_->layout) observing_ = true; else restore(); }
  void set_background(unsigned color) override {
    if (state_->background != color) {
      state_->background = color;
      if (state_->alive) InvalidateRect(state_->editor, nullptr, FALSE);
    }
    if (state_->layout) state_->layout->set_background(color);
  }
  void request_size(int width, int height) override {
    requested_width_ = width; requested_height_ = height;
    if (!native_valid() || (GetWindowLong(state_->owner, GWL_STYLE) & WS_CHILD)) return;
    const auto editor = editor_content(state_->editor);
    RECT owner{}; GetWindowRect(state_->owner, &owner);
    const int dx = std::max(0, width - int(editor.right-editor.left)), dy = std::max(0, height - int(editor.bottom-editor.top));
    if (state_->layout) state_->layout->adjusting = true;
    // REAPER persists a floating window's native origin across editor recreation.
    if (dx || dy) resize_editor_owner(state_->owner, std::abs(int(owner.right-owner.left))+dx,
      std::abs(int(owner.bottom-owner.top))+dy, bool(state_->layout));
    if (state_->layout) {
      state_->layout->remember(); state_->layout->adjusting = false; state_->layout->handoff_until = {};
    }
  }
  void present(void* child) override {
    if (!valid()) return;
    if (!revealed_) request_size(requested_width_, requested_height_);
    resize(child);
    prepare();
  }
  void restore() override {
    restore_native(*state_);
  }
};
class ChainHost final : public JsfxEditorHost {
  std::shared_ptr<ChainLayout> layout_;
  JsfxTarget target_;
  int fx_ = -1;
  bool attached_ = false;
public:
  explicit ChainHost(std::shared_ptr<ChainLayout> layout) : layout_(std::move(layout)), target_(layout_->target), fx_(layout_->active_fx) {}
  ~ChainHost() override { if (!attached_ && layout_->active_host.expired()) restore_chain(layout_); }
  void* parent() const override { return layout_->backdrop; }
  bool valid() const override {
    return layout_->alive && layout_->target == target_ && IsWindow(layout_->owner) && IsWindowVisible(layout_->owner) &&
      layout_->backdrop && IsWindow(layout_->backdrop);
  }
  std::optional<int> selected_fx() const override { return layout_->selected(); }
  void sync_selection(int fx) override {
    if (layout_->active_host.lock().get() == this) { fx_ = fx; layout_->active_fx = fx; }
  }
  void on_destroy(std::function<void()> callback) override { layout_->on_destroy = std::move(callback); }
  void resize(void*) override { if (valid()) layout_->resize_children(); }
  void prepare() override {
    if (!valid()) return;
    layout_->resize_viewport();
    ShowWindow(layout_->backdrop, SW_SHOWNA);
  }
  void present(void* child) override { attached_ = true; prepare(); resize(child); }
  void reveal() override { if (valid()) layout_->reveal(); }
  void park() override { if (layout_->selected() < 0) restore(); }
  bool retain_during_switch() const override {
    return valid() && layout_->selected() >= 0 && !layout_->applies() && !native_editor_ready(*layout_);
  }
  void restore() override {
    if (layout_->active_host.lock().get() == this && layout_->target == target_ && layout_->active_fx == fx_) restore_chain(layout_);
  }
  void set_background(unsigned color) override { layout_->set_background(color); }
  void request_size(int width, int height) override {
    if (layout_->backdrop) layout_->request_size(width, height);
  }
};
void select_chain(const std::shared_ptr<ChainLayout>& layout, const JsfxEditorDiscovery::Match& match, std::vector<JsfxEditor>& found) {
  const int fx = layout->selected();
  if (fx == layout->active_fx && !layout->active_host.expired()) return;
  const auto size = fx >= 0 ? match(layout->target, fx) : std::nullopt;
  if (!size) {
    if (fx >= 0 && layout->backdrop && IsWindowVisible(layout->backdrop) && !native_editor_ready(*layout)) return;
    layout->active_fx = fx; restore_chain(layout); return;
  }
  layout->active_fx = fx;
  for (const auto& entry : attachments) {
    auto& editor = *entry.second;
    if (editor.layout == layout && editor.alive && IsWindowVisible(editor.editor)) {
      editor.prepared = true; editor.conceal();
      layout->cover(editor.editor, size);
    }
  }
  auto host = std::make_shared<ChainHost>(layout);
  layout->active_host = host;
  host->request_size(size->width, size->height);
  layout->handoff();
  found.push_back({layout->target.track, fx, false, layout->owner, std::move(host), layout->target.take, layout->target.input});
}
struct Search {
  Host& host;
  const JsfxEditorDiscovery::Match& match;
  std::vector<JsfxEditor> found;
  int remaining = 8192;
  bool showing = false;
};
BOOL CALLBACK inspect(HWND editor, LPARAM parameter) {
  auto& search = *reinterpret_cast<Search*>(parameter);
  if (--search.remaining < 0) return FALSE;
  if (!search.showing && !IsWindowVisible(editor)) return TRUE;
  if (auto previous = attachments.find(editor); previous != attachments.end()) {
    auto state = previous->second;
    const auto focused = state->layout ? focused_chain_fx(search.host, state->owner) : std::nullopt;
    if (state->layout && !state->prepared && state->layout->applies()) {
      if (const auto size = search.match(state->layout->target, state->layout->selected())) {
        state->prepared = true; state->conceal();
        state->layout->cover(editor, size);
        select_chain(state->layout, search.match, search.found);
      }
    }
    if (!focused || (focused->target == state->layout->target && state->layout->selected() >= 0)) return TRUE;
    auto layout = state->layout;
    auto detach = std::move(layout->on_destroy);
    if (detach) detach();
    restore_chain(layout);
    state->alive = false;
#ifdef _WIN32
    RemoveWindowSubclass(editor, editor_proc, 1);
#else
    if (GetWindowLong(editor, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(editor_proc))
      SetWindowLong(editor, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(state->original));
#endif
    attachments.erase(editor);
    layout->stop(); chain_layouts.erase(layout->owner);
  }
  const auto label = GetDlgItem(editor, 1023), edit = GetDlgItem(editor, 1062);
  if (!label || !edit || GetParent(label) != editor || GetParent(edit) != editor) return TRUE;
#ifdef _WIN32
  DWORD process = 0;
  if (GetWindowThreadProcessId(editor, &process) != GetCurrentThreadId() || process != GetCurrentProcessId()) return TRUE;
  char type[64]{}; GetClassNameA(editor, type, sizeof(type));
  if (std::strcmp(type, "#32770")) return TRUE;
  GetClassNameA(edit, type, sizeof(type));
  if (_stricmp(type, "Button")) return TRUE;
#endif
  HWND owner = editor;
  while (GetParent(owner) && !GetDlgItem(owner, 1076)) {
    // A floating FX is identified by the official handle, before walking into the main window.
    if (known_floating_fx(search.host, owner)) break;
    if (const auto focused = focused_fx(search.host, owner)) {
      if (focused->target.api<void* (*)(void*, int)>(search.host, "GetFloatingWindow")(focused->target.object(), focused->fx) == owner) break;
    }
    owner = GetParent(owner);
  }
  JsfxTarget target; int fx = -1; bool is_floating = false;
  const auto floating = known_floating_fx(search.host, owner);
  if (const auto focused = floating ? floating : focused_chain_fx(search.host, owner)) {
    target = focused->target; fx = focused->fx;
    is_floating = !chain_list(owner) &&
      target.api<void* (*)(void*, int)>(search.host, "GetFloatingWindow")(target.object(), fx) == owner;
  } else if (auto known = chain_layouts.find(owner); known != chain_layouts.end() && known->second->alive && !known->second->frame_only) {
    target = known->second->target; fx = known->second->selected();
  } else {
    using Hit = void* (*)(int, int, char*, int);
    auto hit = reinterpret_cast<Hit>(search.host.native_function("GetThingFromPoint"));
    if (!hit) return TRUE;
    auto point_window = GetDlgItem(owner, 1076);
    if (!point_window) point_window = edit;
    POINT point{4, 4}; ClientToScreen(point_window, &point);
    const auto hit_window = WindowFromPoint(point);
    if (hit_window != point_window && !IsChild(point_window, hit_window)) return TRUE;
    char info[128]{};
    target.track = hit(point.x, point.y, info, sizeof(info));
    if (!target.track || !search.host.valid_track(search.host.current_project(), target.track)) return TRUE;
    if (!std::strcmp(info, "fx_chain")) {
      // The hit-test identifies the track, but does not distinguish its input chain.
      if (target.api<int (*)(void*)>(search.host, "GetRecChainVisible")(target.object()) != -1) return TRUE;
      fx = target.selected(search.host);
    }
    else if (!std::strncmp(info, "fx_", 3)) {
      char* end = nullptr; const auto index = std::strtol(info + 3, &end, 10);
      if (end && !*end && index >= 0 && index <= INT_MAX) {
        fx = int(index); target.input = bool(fx & 0x1000000);
        const auto frame = static_cast<HWND>(target.api<void* (*)(void*, int)>(search.host, "GetFloatingWindow")(target.object(), fx));
        if (!frame || !chain_list(owner) || (frame != owner && !IsChild(frame, owner)) ||
            target.config(search.host, fx, "fx_type") != "Container") {
          owner = frame; is_floating = true;
        }
      }
    }
  }
  if (!is_floating && fx >= 0 && owner && chain_list(owner)) {
    const auto position = chain_position(search.host, target, owner, fx);
    fx = position ? position->selected : -1;
  }
  if (fx < 0 || !owner || (owner != editor && !IsChild(owner, editor))) return TRUE;
  try {
    const auto size = search.match(target, fx);
    if (!size) return TRUE;
    auto layout = !is_floating && editor != owner ? chain_layout(owner, search.host, target, fx, search.match) : nullptr;
    auto host = std::make_shared<EditorHost>(editor, owner, layout);
    host->prepare();
    if (layout) {
      layout->cover(editor, size);
      host->park();
      select_chain(layout, search.match, search.found);
    } else {
      host->request_size(size->width, size->height);
      search.found.push_back({target.track, fx, is_floating, owner, std::move(host), target.take, target.input});
    }
  }
  catch (...) {}
  return TRUE;
}
BOOL CALLBACK inspect_top(HWND window, LPARAM parameter) {
  auto& search = *reinterpret_cast<Search*>(parameter);
  if (search.remaining <= 0) return FALSE;
  inspect(window, parameter);
  EnumChildWindows(window, inspect, parameter);
  return TRUE;
}
class Discovery final : public JsfxEditorDiscovery, public IReaperControlSurface {
  Host* host_ = nullptr;
  Match match_;
  std::vector<JsfxEditor> pending_;
  int (*register_)(const char*, void*) = nullptr;
  std::thread::id main_thread_ = std::this_thread::get_id();
  bool inspecting_ = false;
  static Discovery* active_;
#ifdef _WIN32
  HHOOK hook_ = nullptr;
  DWORD thread_ = GetCurrentThreadId();
  std::map<HWND, Clock::time_point> opening_;
  std::set<HWND> checked_takes_;
  std::optional<FocusedFx> opening_fx_;
  std::optional<FocusedFx> floating_take(HWND window) {
    auto count = reinterpret_cast<int (*)(void*)>(host_->native_function("CountMediaItems"));
    auto item_at = reinterpret_cast<void* (*)(void*, int)>(host_->native_function("GetMediaItem"));
    auto item_track = reinterpret_cast<void* (*)(void*)>(host_->native_function("GetMediaItem_Track"));
    auto takes = reinterpret_cast<int (*)(void*)>(host_->native_function("CountTakes"));
    auto take_at = reinterpret_cast<void* (*)(void*, int)>(host_->native_function("GetTake"));
    if (!count || !item_at || !item_track || !takes || !take_at) return {};
    auto project = host_->current_project();
    int budget = 4096;
    for (int n = 0, total = count(project); n < total && budget > 0; ++n) {
      auto item = item_at(project, n); --budget;
      if (!item) continue;
      auto track = item_track(item);
      if (!host_->valid_track(project, track)) continue;
      for (int t = 0, total_takes = takes(item); t < total_takes && budget > 0; ++t) {
        JsfxTarget target{track, take_at(item, t)}; --budget;
        if (!target.take) continue;
        for (int fx : target.indices(*host_)) {
          if (--budget < 0) return {};
          if (target.api<void* (*)(void*, int)>(*host_, "GetFloatingWindow")(target.take, fx) == window)
            return FocusedFx{target, fx};
        }
      }
    }
    return {};
  }
  void prepare_floating(HWND window, bool showing) {
    if (opening_.count(window) || (GetWindowLong(window, GWL_STYLE) & WS_CHILD)) return;
    char type[32]{}; GetClassNameA(window, type, sizeof(type));
    if (std::strcmp(type, "#32770")) return;
    for (const auto& entry : attachments) if (entry.second->alive && entry.second->owner == window) return;
    auto fx = known_floating_fx(*host_, window);
    if (!fx && opening_fx_ && host_->valid_track(host_->current_project(), opening_fx_->target.track) &&
        opening_fx_->target.api<void* (*)(void*, int)>(*host_, "GetFloatingWindow")(
          opening_fx_->target.object(), opening_fx_->fx) == window) fx = opening_fx_;
    if (!fx && !checked_takes_.count(window) && (showing || IsWindowVisible(window))) {
      fx = floating_take(window);
      if (IsWindowVisible(window)) checked_takes_.insert(window);
    }
    if (!fx || !match_(fx->target, fx->fx)) return;
    const BOOL cloak = TRUE;
    if (SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak, sizeof(cloak)))) opening_[window] = Clock::now();
  }
  void settle_opening() {
    for (auto it = opening_.begin(); it != opening_.end();) {
      if (!IsWindow(it->first)) { it = opening_.erase(it); continue; }
      if (it->second != Clock::time_point{}) {
        bool attached = false;
        for (const auto& entry : attachments)
          if (entry.second->alive && entry.second->prepared && entry.second->owner == it->first) { attached = true; break; }
        if (attached) it->second = {};
        else if (Clock::now() - it->second > std::chrono::seconds(2)) {
          const BOOL cloak = FALSE;
          DwmSetWindowAttribute(it->first, DWMWA_CLOAK, &cloak, sizeof(cloak));
          it->second = {};
        }
      }
      ++it;
    }
  }
  static LRESULT CALLBACK before_paint(int code, WPARAM wp, LPARAM lp) {
    auto self = active_;
    if (code >= 0 && self && reinterpret_cast<CWPSTRUCT*>(lp)->message == WM_NCDESTROY) {
      auto window = reinterpret_cast<CWPSTRUCT*>(lp)->hwnd;
      self->opening_.erase(window); self->checked_takes_.erase(window);
    }
    if (code >= 0 && self && !self->inspecting_ && self->host_) {
      const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
      const bool showing = (message.message == WM_SHOWWINDOW && message.wParam) ||
        (message.message == WM_WINDOWPOSCHANGING && message.lParam &&
          (reinterpret_cast<WINDOWPOS*>(message.lParam)->flags & SWP_SHOWWINDOW));
      if (message.message == WM_PAINT || message.message == WM_ERASEBKGND || message.message == WM_NCPAINT ||
          message.message == WM_WINDOWPOSCHANGED || showing) {
        self->inspecting_ = true;
        try {
          self->prepare_floating(message.hwnd, showing);
          const auto parent = attachments.find(GetParent(message.hwnd));
          if (parent != attachments.end()) parent->second->conceal();
          const bool chain = GetDlgItem(message.hwnd, 1076) != nullptr;
          Search search{*self->host_, self->match_, {}, chain ? 512 : 2, showing || (chain && message.message == WM_WINDOWPOSCHANGED)};
          if (chain) {
            EnumChildWindows(message.hwnd, inspect, reinterpret_cast<LPARAM>(&search));
          }
          else inspect(message.hwnd, reinterpret_cast<LPARAM>(&search));
          search.showing = false;
          if (search.found.empty()) inspect(GetParent(message.hwnd), reinterpret_cast<LPARAM>(&search));
          for (auto& editor : search.found) self->pending_.push_back(std::move(editor));
        } catch (...) {}
        self->inspecting_ = false;
      }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
  }
#else
  std::map<HWND, WNDPROC> probes_;
  int probe_budget_ = 0;
  static LRESULT before_paint(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto self = active_;
    if (!self) return DefWindowProc(window, message, wp, lp);
    auto it = self->probes_.find(window);
    if (it == self->probes_.end()) return DefWindowProc(window, message, wp, lp);
    auto original = it->second;
    // A verified editor/frame can wrap a probe installed through another observer.
    if (attachments.count(window) || chain_layouts.count(window)) {
      if (message == WM_DESTROY) self->probes_.erase(it);
      return original(window, message, wp, lp);
    }
    if (message == WM_DESTROY) {
      self->probes_.erase(it);
      SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    } else if (!self->inspecting_ && (message == WM_PAINT || message == WM_ERASEBKGND ||
               (message == WM_SHOWWINDOW && wp))) {
      // Observe unresolved editors without concealing them until ownership is verified.
      self->probes_.erase(it);
      SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(original));
      self->inspecting_ = true;
      try {
        const bool chain = chain_list(window) != nullptr;
        Search search{*self->host_, self->match_, {}, chain ? 512 : 2, true};
        if (chain) EnumChildWindows(window, inspect, reinterpret_cast<LPARAM>(&search));
        else inspect(window, reinterpret_cast<LPARAM>(&search));
        for (auto& editor : search.found) self->pending_.push_back(std::move(editor));
      } catch (...) {}
      self->inspecting_ = false;
      auto current = reinterpret_cast<WNDPROC>(GetWindowLong(window, GWL_WNDPROC));
      if (current != original) return current(window, message, wp, lp);
      self->probes_[window] = original;
      SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(before_paint));
    }
    return original(window, message, wp, lp);
  }
  static BOOL arm_probe(HWND window, LPARAM parameter) {
    auto& self = *reinterpret_cast<Discovery*>(parameter);
    if (--self.probe_budget_ < 0) return FALSE;
    if (attachments.count(window) || chain_layouts.count(window) || self.probes_.count(window)) return TRUE;
    auto label = GetDlgItem(window, 1023), edit = GetDlgItem(window, 1062);
    if (!chain_list(window) && (!label || !edit || GetParent(label) != window || GetParent(edit) != window)) return TRUE;
    auto original = reinterpret_cast<WNDPROC>(GetWindowLong(window, GWL_WNDPROC));
    if (original) {
      self.probes_[window] = original;
      SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(before_paint));
    }
    return TRUE;
  }
  static BOOL arm_top(HWND window, LPARAM parameter) {
    if (!arm_probe(window, parameter)) return FALSE;
    EnumChildWindows(window, arm_probe, parameter);
    return TRUE;
  }
#endif
public:
  const char* GetTypeString() override { return "REAWEBJSFX"; }
  const char* GetDescString() override { return "ReaWebAPI JSFX editor observer"; }
  const char* GetConfigString() override { return ""; }
  int Extended(int call, void* track, void* index, void* opened) override {
    if ((call != CSURF_EXT_SETFXOPEN && call != CSURF_EXT_SETFOCUSEDFX) || !opened || !track ||
        !host_ || inspecting_ || std::this_thread::get_id() != main_thread_) return 0;
#ifdef _WIN32
    if (call == CSURF_EXT_SETFXOPEN && index) {
      const int fx = *static_cast<int*>(index);
      if (fx >= 0) opening_fx_ = FocusedFx{{track, nullptr, bool(fx & 0x1000000)}, fx};
    }
#else
    (void)index;
#endif
    // Resolve ownership at the host notification, before the editor's first paint.
    inspecting_ = true;
    Search search{*host_, match_, {}, 512, true};
    try {
#ifdef _WIN32
      EnumThreadWindows(thread_, inspect_top, reinterpret_cast<LPARAM>(&search));
#else
      EnumWindows(inspect_top, reinterpret_cast<LPARAM>(&search));
      probe_budget_ = 512;
      EnumWindows(arm_top, reinterpret_cast<LPARAM>(this));
#endif
    }
    catch (...) {}
    inspecting_ = false;
    for (auto& editor : search.found) pending_.push_back(std::move(editor));
    return 0;
  }
  bool pending() const override {
    if (!pending_.empty()) return true;
    for (const auto& entry : chain_layouts) if (entry.second->alive && IsWindowVisible(entry.first) &&
        entry.second->selected() != entry.second->active_fx) return true;
    return false;
  }
  ~Discovery() override {
    if (register_) register_("-csurf_inst", static_cast<IReaperControlSurface*>(this));
#ifdef _WIN32
    if (hook_) UnhookWindowsHookEx(hook_);
    for (const auto& entry : opening_) if (IsWindow(entry.first)) {
      const BOOL cloak = FALSE;
      DwmSetWindowAttribute(entry.first, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }
#else
    for (const auto& [window, original] : probes_)
      if (IsWindow(window) && GetWindowLong(window, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(before_paint))
        SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(original));
#endif
    if (active_ == this) {
      active_ = nullptr;
#ifndef _WIN32
      detach_editor_probe = {};
#endif
    }
    const auto observed = attachments;
    for (auto& entry : observed) {
      auto& state = *entry.second;
      restore_native(state);
      if (!state.alive || !IsWindow(state.editor)) continue;
#ifdef _WIN32
      RemoveWindowSubclass(state.editor, editor_proc, 1);
#else
      if (GetWindowLong(state.editor, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(editor_proc))
        SetWindowLong(state.editor, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(state.original));
#endif
    }
    attachments.clear();
    for (auto& entry : chain_layouts) entry.second->stop();
    chain_layouts.clear();
  }
  std::vector<JsfxEditor> scan(Host& host, Match match) override {
  if (!host.native_function) return {};
  auto version = reinterpret_cast<const char* (*)()>(host.native_function("GetAppVersion"));
  if (!version) return {};
  // Private editor controls are accepted only within the supported REAPER 7 layout family.
  const auto number = std::strtod(version(), nullptr);
  if (number < 7.82 || number >= 8) return {};
  host_ = &host; match_ = std::move(match);
  active_ = this;
#ifdef _WIN32
  settle_opening();
#endif
#ifndef _WIN32
  detach_editor_probe = [this](HWND window) {
    auto it = probes_.find(window);
    if (it != probes_.end() && GetWindowLong(window, GWL_WNDPROC) == reinterpret_cast<LONG_PTR>(before_paint)) {
      SetWindowLong(window, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(it->second));
      probes_.erase(it);
    }
  };
#endif
  Search search{host, match_, std::move(pending_), 8192};
  pending_.clear();
  inspecting_ = true;
  for (const auto& entry : chain_layouts) if (entry.second->alive && IsWindowVisible(entry.first))
    select_chain(entry.second, match_, search.found);
  if (!register_) {
    auto registration = reinterpret_cast<int (*)(const char*, void*)>(host.native_function("plugin_register"));
    if (registration && registration("csurf_inst", static_cast<IReaperControlSurface*>(this))) register_ = registration;
  }
#ifdef _WIN32
  if (!hook_) { active_ = this; hook_ = SetWindowsHookExW(WH_CALLWNDPROC, before_paint, nullptr, GetCurrentThreadId()); }
  EnumThreadWindows(GetCurrentThreadId(), inspect_top, reinterpret_cast<LPARAM>(&search));
#else
  EnumWindows(inspect_top, reinterpret_cast<LPARAM>(&search));
#endif
  inspecting_ = false;
  return search.found;
  }
};
Discovery* Discovery::active_ = nullptr;
}
JsfxDiscovery make_jsfx_editor_discovery() {
  return std::make_shared<Discovery>();
}
}
