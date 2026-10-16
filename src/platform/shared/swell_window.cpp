#include "platform/shared/swell_window.hpp"
#include <swell/swell-dlggen.h>
#include <algorithm>

static SWELL_DialogRegHelper reaweb_dialog(&SWELL_curmodule_dialogresource_head,
  [](HWND, int) {}, 101, SWELL_DLG_WS_RESIZABLE, "ReaWebAPI", 860, 640, 1.0, 1.0);
static SWELL_DialogRegHelper reaweb_hosted_dialog(&SWELL_curmodule_dialogresource_head,
  [](HWND, int) {}, 102, SWELL_DLG_WS_CHILD, "ReaWebAPI", 860, 640, 1.0, 1.0);

namespace reaweb {
INT_PTR SwellWindow::procedure(HWND window, UINT message, WPARAM command, LPARAM parameter) {
  if (message == WM_INITDIALOG) { SetWindowLong(window, GWL_USERDATA, parameter); return TRUE; }
  auto self = reinterpret_cast<SwellWindow*>(GetWindowLong(window, GWL_USERDATA));
  if (!self) return FALSE;
  if (message == WM_ERASEBKGND) return TRUE;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    if (auto dc = BeginPaint(window, &paint)) {
      if (!self->transparent_) {
        const auto color = self->background_;
        auto brush = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
        FillRect(dc, &paint.rcPaint, brush); DeleteObject(brush);
      }
      EndPaint(window, &paint);
    }
    return TRUE;
  }
  if (message == WM_SIZE && self->resize) self->resize();
  if (message == WM_CONTEXTMENU && self->context_menu) {
    auto show = self->context_menu;
    show(parameter); return TRUE;
  }
  if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(command) == IDCANCEL && !HIWORD(command) && !parameter)) {
    if (self->close_) self->close_(); else self->closed_ = true;
    return TRUE;
  }
  if (message == WM_DESTROY) {
    if (self->before_destroy) self->before_destroy();
    self->closed_ = true; self->window_ = nullptr;
  }
  if (message == WM_SETFOCUS && self->focus_) self->focus_();
  return FALSE;
}
SwellWindow::SwellWindow(const std::string& title, void* parent, std::function<void()> focus, std::function<void()> close, bool hidden, bool hosted)
  : owner_(static_cast<HWND>(parent)), focus_(std::move(focus)), close_(std::move(close)) {
  window_ = CreateDialogParam(nullptr, MAKEINTRESOURCE(hosted ? 102 : 101), static_cast<HWND>(parent), procedure, reinterpret_cast<LPARAM>(this));
  if (!window_) throw std::runtime_error("Cannot create the REAPER WebView container");
  SetWindowText(window_, title.c_str());
  if (hosted) {
    RECT rect{}; GetClientRect(static_cast<HWND>(parent), &rect);
    SetWindowPos(window_, HWND_TOP, 0, 0, rect.right, rect.bottom, SWP_NOACTIVATE);
  }
  // Backends can finish Docker registration and first-frame preparation before showing.
  if (!hidden) ShowWindow(window_, SW_SHOW);
}
SwellWindow::~SwellWindow() {
  if (window_ && IsWindow(window_)) {
    SetWindowLong(window_, GWL_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (hosted_parking_ && IsWindow(hosted_parking_)) DestroyWindow(hosted_parking_);
}
bool SwellWindow::closed() const { return closed_ || !window_ || !IsWindow(window_); }
void SwellWindow::set_background(unsigned color) {
  if (background_ == color) return;
  background_ = color;
  if (window_) InvalidateRect(window_, nullptr, FALSE);
}
void SwellWindow::prepare_dock() { GetWindowRect(window_, &floating_); }
void SwellWindow::restore_floating(bool show) {
  SetParent(window_, nullptr);
  SetWindowLong(window_, GWL_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner_));
  if (configure_floating) configure_floating();
  SetWindowPos(window_, nullptr, floating_.left, floating_.top,
    floating_.right - floating_.left, floating_.bottom - floating_.top, SWP_NOZORDER | SWP_NOACTIVATE);
  if (show) {
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
  }
  restore_placement(placement());
}
void SwellWindow::focus() {
  if (closed()) return;
  ShowWindow(window_, SW_SHOW);
  SetForegroundWindow(window_);
  SetFocus(window_);
  if (focus_) focus_();
}
void SwellWindow::set_title(const std::string& title) { SetWindowText(window_, title.c_str()); }
void SwellWindow::set_visible(bool visible) { ShowWindow(window_, visible ? SW_SHOWNOACTIVATE : SW_HIDE); }
void SwellWindow::reparent_host(void* parent) {
  if (closed()) throw Error("FX_STALE", "Hosted window is no longer available");
  if (!parent) {
    if (!hosted_parking_) {
      hosted_parking_ = CreateDialogParam(nullptr, MAKEINTRESOURCE(101), nullptr, procedure, 0);
      if (hosted_parking_) { ShowWindow(hosted_parking_, SW_HIDE); EnableWindow(hosted_parking_, FALSE); }
    }
    parent = hosted_parking_;
  }
  if (!parent || !IsWindow(static_cast<HWND>(parent))) throw Error("FX_STALE", "Hosted parent is no longer available");
  ShowWindow(window_, SW_HIDE);
  SetParent(window_, static_cast<HWND>(parent));
  if (GetParent(window_) != parent) throw Error("HOST_UNAVAILABLE", "Cannot reparent the hosted window");
  owner_ = static_cast<HWND>(parent);
}
bool SwellWindow::visible() const { return !closed() && IsWindowVisible(window_); }
bool SwellWindow::focused() const {
  auto focus = GetFocus();
  return !closed() && (focus == window_ || IsChild(window_, focus));
}
Json SwellWindow::placement() const {
  if (!window_) return nullptr;
  RECT rect{};
  if (!GetWindowRect(window_, &rect)) return nullptr;
  return {{"x", rect.left}, {"y", rect.top}, {"width", rect.right - rect.left}, {"height", rect.bottom - rect.top}, {"maximized", false}};
}
void SwellWindow::restore_placement(const Json& value) {
  if (value.is_null()) return;
  RECT rect{value.at("x").get<int>(), value.at("y").get<int>(), 0, 0}, viewport{};
  const int width = value.at("width"), height = value.at("height");
  rect.right = rect.left + width; rect.bottom = rect.top + height;
  SWELL_GetViewPort(&viewport, &rect, true);
  const auto w = std::min(width, static_cast<int>(viewport.right - viewport.left));
  const auto h = std::min(height, static_cast<int>(viewport.bottom - viewport.top));
  rect.left = std::clamp(rect.left, viewport.left, viewport.right - w);
  rect.top = std::clamp(rect.top, viewport.top, viewport.bottom - h);
  rect.right = rect.left + w; rect.bottom = rect.top + h;
  floating_ = rect;
  SetWindowPos(window_, nullptr, rect.left, rect.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}
}
