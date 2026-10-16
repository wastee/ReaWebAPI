#include "platform/shared/jsfx_editor.hpp"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <reaper_plugin.h>
#else
#include <swell/swell.h>
#include "platform/shared/swell_window.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

namespace reaweb {
namespace {
using Clock = std::chrono::steady_clock;
void resize_editor_owner(HWND owner, int width, int height) {
#ifdef __APPLE__
  // SWELL uses Cocoa screen coordinates for top-level windows. Keep the upper edge fixed.
  RECT rect{}; GetWindowRect(owner, &rect);
  SetWindowPos(owner, nullptr, rect.left, rect.bottom - height, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
#else
  SetWindowPos(owner, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}
struct ChainLayout {
  HWND owner = nullptr;
  HWND backdrop = nullptr;
  void* track = nullptr;
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
  bool alive = true, adjusting = false, user_sizing = false;
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
    if (!alive || adjusting || !IsWindow(owner) || (GetWindowLong(owner, GWL_STYLE) & WS_CHILD)) return;
    RECT client{}, outer{}; GetClientRect(owner, &client); GetWindowRect(owner, &outer);
    const int dx = std::max(0, requested_width - int(client.right-margins.left-margins.right));
    const int dy = std::max(0, requested_height - int(client.bottom-margins.top-margins.bottom));
    adjusting = true;
    if (dx || dy) resize_editor_owner(owner, std::abs(int(outer.right-outer.left))+dx,
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
    RECT r{}; GetWindowRect(owner, &r);
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
        (GetWindowLong(owner, GWL_STYLE) & WS_CHILD)) return false;
    try { return applies(); } catch (...) { return false; }
  }
};
bool native_editor_ready(const ChainLayout& layout);
void restore_chain(const std::shared_ptr<ChainLayout>& layout);
std::map<HWND, std::shared_ptr<ChainLayout>> chain_layouts;
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
  if (!alive) return;
  if (!backdrop) {
    RECT rect{}, parent{}; GetWindowRect(editor, &rect); GetClientRect(owner, &parent);
    POINT origin{rect.left, rect.top}; ScreenToClient(owner, &origin);
    margins = {origin.x, origin.y, parent.right-origin.x-std::abs(int(rect.right-rect.left)),
      parent.bottom-origin.y-std::abs(int(rect.bottom-rect.top))};
    // Creating or positioning a child can synchronously paint its owner.
    if (size) request_size(size->width, size->height);
#ifdef _WIN32
    backdrop = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
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
#ifdef __APPLE__
  // Capture the position before the host resizes a native editor during selection.
  if (selection && !state->adjusting) {
    try { if (state->selected() != state->active_fx && state->applies()) state->handoff(); } catch (...) {}
  }
#endif
#ifdef _WIN32
  if (message == WM_ENTERSIZEMOVE) { state->user_sizing = true; state->handoff_until = {}; }
  if (message == WM_EXITSIZEMOVE) state->user_sizing = false;
  if (message == WM_SYSCOMMAND || message == WM_DPICHANGED) state->handoff_until = {};
  if (message == WM_WINDOWPOSCHANGING && state->holding()) {
    auto& position = *reinterpret_cast<WINDOWPOS*>(lp);
    if (!(position.flags & SWP_NOSIZE)) { position.cx = state->width; position.cy = state->height; }
  }
#else
  // SWELL delivers WM_SIZE synchronously, before laying out the editor's children.
  if (message == WM_SIZE && state->holding()) {
    RECT r{}; GetWindowRect(window, &r);
    if (std::abs(int(r.right-r.left)) != state->width || std::abs(int(r.bottom-r.top)) != state->height) {
      state->adjusting = true;
#ifdef __APPLE__
      SetWindowPos(window, nullptr, state->upper_left.x, state->upper_left.y-state->height,
        state->width, state->height, SWP_NOZORDER | SWP_NOACTIVATE);
#else
      resize_editor_owner(window, state->width, state->height);
#endif
      state->adjusting = false;
      return 0;
    }
  }
#endif
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
  if (state->alive && message == WM_SIZE) { state->remember(); state->resize_viewport(); }
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
std::shared_ptr<ChainLayout> chain_layout(HWND owner, Host& host, void* track, JsfxEditorDiscovery::Match match) {
  auto it = chain_layouts.find(owner);
  auto state = it != chain_layouts.end() ? it->second : std::make_shared<ChainLayout>();
  if (state->track != track) { state->active_fx = -1; state->active_host.reset(); }
  state->owner = owner; state->track = track; state->remember();
  auto project = host.current_project();
  state->selected = [&host, project, track, owner] {
    if (host.current_project() != project || !host.valid_track(project, track)) return -1;
    auto count = reinterpret_cast<int (*)(void*)>(host.native_function("TrackFX_GetCount"));
    auto list = GetDlgItem(owner, 1076);
    if (!list || !count || ListView_GetItemCount(list) != count(track)) return -1;
    const int focused = ListView_GetNextItem(list, -1, LVNI_SELECTED | LVNI_FOCUSED);
    const int selected = focused >= 0 ? focused : ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected >= 0) return selected;
    // Clearing the list selection does not close REAPER's current FX editor.
    auto visible = reinterpret_cast<int (*)(void*)>(host.native_function("TrackFX_GetChainVisible"));
    const int current = visible ? visible(track) : -1;
    return current >= 0 && current < count(track) ? current : -1;
  };
  state->applies = [selected = state->selected, track, match = std::move(match)] {
    const auto fx = selected();
    return fx >= 0 && match(track, fx);
  };
  if (it != chain_layouts.end()) return state;
#ifdef _WIN32
  if (!SetWindowSubclass(owner, chain_proc, 1, 0)) return {};
#else
  state->original = reinterpret_cast<WNDPROC>(GetWindowLong(owner, GWL_WNDPROC));
  if (!state->original) return {};
  SetWindowLong(owner, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(chain_proc));
#endif
  chain_layouts.emplace(owner, state);
  return state;
}
struct Attachment {
  HWND editor = nullptr, child = nullptr, owner = nullptr;
  std::vector<HWND> hidden;
  bool alive = true, prepared = false;
  unsigned background = 0xffffff;
  std::function<void()> on_destroy;
  std::shared_ptr<ChainLayout> layout;
#ifndef _WIN32
  WNDPROC original = nullptr;
#endif
  void resize() {
    if (!alive || !child || !IsWindow(child) || GetParent(child) != editor) return;
    RECT r{}; GetClientRect(editor, &r);
    SetWindowPos(child, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE);
    if (layout) layout->cover(editor);
  }
  void conceal() {
    if (!alive || !prepared) return;
    for (auto c = GetWindow(editor, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
      if (c != child && (child || GetWindowLong(c, GWL_ID) != 0) && (GetWindowLong(c, GWL_STYLE) & WS_VISIBLE)) {
        if (std::find(hidden.begin(), hidden.end(), c) == hidden.end()) hidden.push_back(c);
        ShowWindow(c, SW_HIDE);
      }
  }
};
std::map<HWND, std::shared_ptr<Attachment>> attachments;
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
  state.child = nullptr; state.prepared = false;
  if (state.alive && IsWindow(state.editor)) {
    for (auto child : state.hidden) if (IsWindow(child) && GetParent(child) == state.editor) ShowWindow(child, SW_SHOWNA);
    InvalidateRect(state.editor, nullptr, TRUE);
  }
  state.hidden.clear();
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
  if (state->prepared && state->layout && (message == WM_DESTROY || (message == WM_SHOWWINDOW && !wp)))
    state->layout->handoff();
  if (state->prepared && message == WM_SHOWWINDOW && wp) state->conceal();
  const auto fill_background = [&](HDC dc, const RECT& rect) {
    if (!dc) return;
    const auto color = state->background;
    auto brush = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
    FillRect(dc, &rect, brush); DeleteObject(brush);
  };
  if (state->prepared && message == WM_ERASEBKGND) {
    if (!state->child || !IsWindowVisible(state->child)) {
      RECT r{}; GetClientRect(window, &r); fill_background(reinterpret_cast<HDC>(wp), r);
    }
    return 1;
  }
  if (state->prepared && message == WM_PAINT) {
    state->conceal();
    PAINTSTRUCT paint{};
    if (auto dc = BeginPaint(window, &paint)) {
      if (!state->child || !IsWindowVisible(state->child)) {
        fill_background(dc, paint.rcPaint);
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
      ) state->resize();
  return result;
}
class EditorHost final : public JsfxEditorHost {
  std::shared_ptr<Attachment> state_;
  bool observing_ = false;
public:
  EditorHost(HWND editor, HWND owner, std::shared_ptr<ChainLayout> layout) : state_(std::make_shared<Attachment>()) {
    state_->editor = editor; state_->owner = owner;
    state_->layout = std::move(layout);
    if (state_->layout) state_->background = state_->layout->background;
    if (attachments.count(editor)) throw Error("EDITOR_BUSY", "JSFX editor already attached");
#ifdef _WIN32
    if (!SetWindowSubclass(editor, editor_proc, 1, 0)) throw Error("EDITOR_UNAVAILABLE", "Cannot observe JSFX editor lifetime");
#else
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
    return state_->alive && IsWindow(state_->editor) && IsWindow(state_->owner) && IsWindowVisible(state_->editor) &&
      (state_->owner == state_->editor || IsChild(state_->owner, state_->editor)) &&
      GetDlgItem(state_->editor, 1023) && GetDlgItem(state_->editor, 1062);
  }
  void resize(void* child) override { state_->child = static_cast<HWND>(child); state_->resize(); }
  void prepare() override { state_->prepared = true; state_->conceal(); }
  void park() override { if (state_->layout) observing_ = true; else restore(); }
  void set_background(unsigned color) override {
    if (state_->background != color) {
      state_->background = color;
      if (state_->alive) InvalidateRect(state_->editor, nullptr, FALSE);
    }
    if (state_->layout) state_->layout->set_background(color);
  }
  void request_size(int width, int height) override {
    if (!valid() || (GetWindowLong(state_->owner, GWL_STYLE) & WS_CHILD)) return;
    RECT editor{}, owner{}; GetClientRect(state_->editor, &editor); GetWindowRect(state_->owner, &owner);
    const int dx = std::max(0, width - int(editor.right)), dy = std::max(0, height - int(editor.bottom));
    if (state_->layout) state_->layout->adjusting = true;
    if (dx || dy) resize_editor_owner(state_->owner, std::abs(int(owner.right-owner.left))+dx,
      std::abs(int(owner.bottom-owner.top))+dy);
    if (state_->layout) {
      state_->layout->remember(); state_->layout->adjusting = false; state_->layout->handoff_until = {};
    }
  }
  void present(void* child) override {
    if (!valid()) return;
    resize(child);
    prepare();
  }
  void restore() override {
    restore_native(*state_);
  }
};
class ChainHost final : public JsfxEditorHost {
  std::shared_ptr<ChainLayout> layout_;
  void* track_ = nullptr;
  int fx_ = -1;
  bool attached_ = false;
public:
  explicit ChainHost(std::shared_ptr<ChainLayout> layout) : layout_(std::move(layout)), track_(layout_->track), fx_(layout_->active_fx) {}
  ~ChainHost() override { if (!attached_ && layout_->active_host.expired()) restore_chain(layout_); }
  void* parent() const override { return layout_->backdrop; }
  bool valid() const override {
    return layout_->alive && layout_->track == track_ && IsWindow(layout_->owner) && IsWindowVisible(layout_->owner) &&
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
  void park() override {}
  bool retain_during_switch() const override {
    return valid() && layout_->selected() >= 0 && !layout_->applies() && !native_editor_ready(*layout_);
  }
  void restore() override {
    if (layout_->track == track_ && layout_->active_fx == fx_) restore_chain(layout_);
  }
  void set_background(unsigned color) override { layout_->set_background(color); }
  void request_size(int width, int height) override {
    if (layout_->backdrop) layout_->request_size(width, height);
  }
};
void select_chain(const std::shared_ptr<ChainLayout>& layout, const JsfxEditorDiscovery::Match& match, std::vector<JsfxEditor>& found) {
  const int fx = layout->selected();
  if (fx == layout->active_fx && !layout->active_host.expired()) return;
  const auto size = fx >= 0 ? match(layout->track, fx) : std::nullopt;
  if (!size) {
    if (fx >= 0 && layout->backdrop && IsWindowVisible(layout->backdrop) && !native_editor_ready(*layout)) return;
    layout->active_fx = fx; restore_chain(layout); return;
  }
  layout->active_fx = fx;
  auto host = std::make_shared<ChainHost>(layout);
  layout->active_host = host;
  host->request_size(size->width, size->height);
  layout->handoff();
  found.push_back({layout->track, fx, false, layout->owner, std::move(host)});
}
struct Search {
  Host& host;
  const JsfxEditorDiscovery::Match& match;
  std::vector<JsfxEditor> found;
  int remaining = 8192;
  bool showing = false;
  HWND opening_owner = nullptr;
  void* opening_track = nullptr;
  int opening_fx = -1;
};
BOOL CALLBACK inspect(HWND editor, LPARAM parameter) {
  auto& search = *reinterpret_cast<Search*>(parameter);
  if (--search.remaining < 0) return FALSE;
  if (attachments.count(editor) || (!search.showing && !IsWindowVisible(editor))) return TRUE;
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
  using Hit = void* (*)(int, int, char*, int);
  using Chain = int (*)(void*);
  using Floating = void* (*)(void*, int);
  auto hit = reinterpret_cast<Hit>(search.host.native_function("GetThingFromPoint"));
  auto chain = reinterpret_cast<Chain>(search.host.native_function("TrackFX_GetChainVisible"));
  auto floating = reinterpret_cast<Floating>(search.host.native_function("TrackFX_GetFloatingWindow"));
  if (!hit || !chain || !floating) return FALSE;
  HWND owner = editor;
  while (GetParent(owner) && !GetDlgItem(owner, 1076)) owner = GetParent(owner);
  void* track = nullptr;
  int fx = -1; bool is_floating = false;
  if (owner == search.opening_owner) {
    track = search.opening_track; fx = search.opening_fx;
  } else {
  auto point_window = GetDlgItem(owner, 1076);
  if (!point_window) point_window = edit;
  POINT point{4, 4}; ClientToScreen(point_window, &point);
  const auto hit_window = WindowFromPoint(point);
  if (hit_window != point_window && !IsChild(point_window, hit_window)) return TRUE;
  char info[128]{};
  track = hit(point.x, point.y, info, sizeof(info));
  if (!track || !search.host.valid_track(search.host.current_project(), track)) return TRUE;
  if (!std::strcmp(info, "fx_chain")) fx = chain(track);
  else if (!std::strncmp(info, "fx_", 3)) {
    char* end = nullptr; auto index = std::strtol(info + 3, &end, 10);
    if (end && !*end && index >= 0 && index < 4096) {
      fx = static_cast<int>(index); is_floating = true;
      owner = static_cast<HWND>(floating(track, fx));
    }
  }
  }
  if (fx < 0 || !owner || (owner != editor && !IsChild(owner, editor))) return TRUE;
  try {
    const auto size = search.match(track, fx);
    if (!size) return TRUE;
    auto layout = !is_floating && editor != owner ? chain_layout(owner, search.host, track, search.match) : nullptr;
    auto host = std::make_shared<EditorHost>(editor, owner, layout);
    host->prepare();
    if (layout) {
      layout->cover(editor, size);
      host->park();
      select_chain(layout, search.match, search.found);
    } else search.found.push_back({track, fx, is_floating, owner, std::move(host)});
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
class Discovery final : public JsfxEditorDiscovery
#ifdef _WIN32
  , public IReaperControlSurface
#endif
{
  Host* host_ = nullptr;
  Match match_;
  std::vector<JsfxEditor> pending_;
#ifdef _WIN32
  HHOOK hook_ = nullptr;
  int (*register_)(const char*, void*) = nullptr;
  DWORD thread_ = GetCurrentThreadId();
  bool inspecting_ = false;
  static Discovery* active_;
  static LRESULT CALLBACK before_paint(int code, WPARAM wp, LPARAM lp) {
    auto self = active_;
    if (code >= 0 && self && !self->inspecting_ && self->host_) {
      const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
      const bool showing = message.message == WM_SHOWWINDOW && message.wParam;
      if (message.message == WM_PAINT || message.message == WM_ERASEBKGND || message.message == WM_NCPAINT ||
          message.message == WM_WINDOWPOSCHANGED || showing) {
        self->inspecting_ = true;
        try {
          const auto parent = attachments.find(GetParent(message.hwnd));
          if (parent != attachments.end()) parent->second->conceal();
          const bool chain = GetDlgItem(message.hwnd, 1076) != nullptr;
          Search search{*self->host_, self->match_, {}, chain ? 512 : 2, showing};
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
#endif
public:
#ifdef _WIN32
  const char* GetTypeString() override { return "REAWEBJSFX"; }
  const char* GetDescString() override { return "ReaWebAPI JSFX editor observer"; }
  const char* GetConfigString() override { return ""; }
  int Extended(int call, void* track, void* index, void* opened) override {
    if (call != CSURF_EXT_SETFXOPEN || !opened || !track || !index || !host_ || inspecting_ ||
        GetCurrentThreadId() != thread_) return 0;
    const int fx = *static_cast<int*>(index);
    std::shared_ptr<ChainLayout> layout;
    try {
      if (fx < 0 || !host_->valid_track(host_->current_project(), track) || !match_(track, fx)) return 0;
      auto floating = reinterpret_cast<void* (*)(void*, int)>(host_->native_function("TrackFX_GetFloatingWindow"));
      if (!floating || floating(track, fx)) return 0;
      // The host announces the editor before showing its newly created FX chain.
      struct Candidate { HWND owner = nullptr; int count = 0, fx_count = 0, fx = -1; } candidate;
      auto count = reinterpret_cast<int (*)(void*)>(host_->native_function("TrackFX_GetCount"));
      if (!count) return 0;
      candidate.fx_count = count(track); candidate.fx = fx;
      EnumThreadWindows(thread_, [](HWND owner, LPARAM data) -> BOOL {
        auto& candidate = *reinterpret_cast<Candidate*>(data);
        const auto list = GetDlgItem(owner, 1076);
        if (IsWindowVisible(owner) || !list || GetParent(list) != owner || (GetWindowLong(owner, GWL_STYLE) & WS_CHILD)) return TRUE;
        char type[64]{}; GetClassNameA(owner, type, sizeof(type));
        if (std::strcmp(type, "#32770") || ListView_GetItemCount(list) != candidate.fx_count ||
            ListView_GetNextItem(list, -1, LVNI_SELECTED) != candidate.fx) return TRUE;
        candidate.owner = owner; ++candidate.count; return TRUE;
      }, reinterpret_cast<LPARAM>(&candidate));
      if (candidate.count != 1 || chain_layouts.count(candidate.owner)) return 0;
      layout = chain_layout(candidate.owner, *host_, track, match_);
      if (!layout) return 0;
      DWORD existing = 0;
      if (SUCCEEDED(DwmGetWindowAttribute(candidate.owner, DWMWA_CLOAKED, &existing, sizeof(existing))) && !existing) {
        const BOOL cloak = TRUE;
        layout->initial_cloak = SUCCEEDED(DwmSetWindowAttribute(candidate.owner, DWMWA_CLOAK, &cloak, sizeof(cloak)));
      }
      inspecting_ = true;
      Search search{*host_, match_, {}, 512, true, candidate.owner, track, fx};
      try { EnumChildWindows(candidate.owner, inspect, reinterpret_cast<LPARAM>(&search)); }
      catch (...) { layout->reveal(); }
      inspecting_ = false;
      if (search.found.empty()) layout->reveal();
      for (auto& editor : search.found) pending_.push_back(std::move(editor));
    } catch (...) { inspecting_ = false; if (layout) layout->reveal(); }
    return 0;
  }
#endif
  bool pending() const override {
    if (!pending_.empty()) return true;
    for (const auto& entry : chain_layouts) if (entry.second->alive && IsWindowVisible(entry.first) &&
        entry.second->selected() != entry.second->active_fx) return true;
    return false;
  }
  ~Discovery() override {
#ifdef _WIN32
    if (register_) register_("-csurf_inst", static_cast<IReaperControlSurface*>(this));
    if (hook_) UnhookWindowsHookEx(hook_);
    if (active_ == this) active_ = nullptr;
#endif
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
  Search search{host, match_, std::move(pending_), 8192};
  pending_.clear();
  for (const auto& entry : chain_layouts) if (entry.second->alive && IsWindowVisible(entry.first))
    select_chain(entry.second, match_, search.found);
#ifdef _WIN32
  if (!hook_) { active_ = this; hook_ = SetWindowsHookExW(WH_CALLWNDPROC, before_paint, nullptr, GetCurrentThreadId()); }
  if (!register_) {
    auto registration = reinterpret_cast<int (*)(const char*, void*)>(host.native_function("plugin_register"));
    if (registration && registration("csurf_inst", static_cast<IReaperControlSurface*>(this))) register_ = registration;
  }
  inspecting_ = true;
  EnumThreadWindows(GetCurrentThreadId(), inspect_top, reinterpret_cast<LPARAM>(&search));
  inspecting_ = false;
#else
  EnumWindows(inspect_top, reinterpret_cast<LPARAM>(&search));
#endif
  return search.found;
  }
};
#ifdef _WIN32
Discovery* Discovery::active_ = nullptr;
#endif
}
JsfxDiscovery make_jsfx_editor_discovery() {
  return std::make_shared<Discovery>();
}
}
