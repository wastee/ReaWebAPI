#include "platform/shared/swell_window.hpp"
#ifdef __linux__
#include <cairo-xlib.h>
#include <dlfcn.h>
#endif
#include <swell/swell-dlggen.h>
#include <algorithm>

static SWELL_DialogRegHelper reaweb_dialog(&SWELL_curmodule_dialogresource_head,
  [](HWND, int) {}, 101, SWELL_DLG_WS_RESIZABLE, "ReaWebAPI", 860, 640, 1.0, 1.0);

namespace reaweb {
INT_PTR SwellWindow::procedure(HWND window, UINT message, WPARAM command, LPARAM parameter) {
  if (message == WM_INITDIALOG) { SetWindowLong(window, GWL_USERDATA, parameter); return TRUE; }
  auto self = reinterpret_cast<SwellWindow*>(GetWindowLong(window, GWL_USERDATA));
  if (!self) return FALSE;
  if (message == WM_ERASEBKGND) return TRUE;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    if (auto dc = BeginPaint(window, &paint)) {
      const auto color = self->background_;
      auto brush = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
      FillRect(dc, &paint.rcPaint, brush); DeleteObject(brush);
#ifdef __linux__
      if (!self->startup_pixels_.empty()) {
        RECT rect{}; GetClientRect(window, &rect);
        StretchBltFromMem(dc, 0, 0, rect.right, rect.bottom, self->startup_pixels_.data(),
          self->startup_width_, self->startup_height_, self->startup_width_);
      }
#endif
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
  if (message == WM_DESTROY) { self->closed_ = true; self->window_ = nullptr; }
  if (message == WM_SETFOCUS && self->focus_) self->focus_();
  return FALSE;
}
SwellWindow::SwellWindow(const std::string& title, void* parent, std::function<void()> focus, std::function<void()> close, bool hidden)
  : owner_(static_cast<HWND>(parent)), focus_(std::move(focus)), close_(std::move(close)) {
  window_ = CreateDialogParam(nullptr, MAKEINTRESOURCE(101), static_cast<HWND>(parent), procedure, reinterpret_cast<LPARAM>(this));
  if (!window_) throw std::runtime_error("Cannot create the REAPER WebView container");
  SetWindowText(window_, title.c_str());
  // Backends can finish Docker registration and first-frame preparation before showing.
  if (!hidden) ShowWindow(window_, SW_SHOW);
}
SwellWindow::~SwellWindow() {
  if (window_ && IsWindow(window_)) {
    SetWindowLong(window_, GWL_USERDATA, 0);
    DestroyWindow(window_);
  }
}
bool SwellWindow::closed() const { return closed_ || !window_ || !IsWindow(window_); }
#ifdef __linux__
void SwellWindow::set_startup_frame(int width, int height, std::string pixels) {
  if (width < 1 || height < 1 || width > 4096 || height > 4096 || pixels.size() != size_t(width) * height * 4)
    throw std::runtime_error("Invalid initial WebKit frame");
  startup_width_ = width; startup_height_ = height; startup_pixels_ = std::move(pixels);
}
void SwellWindow::show_startup_frame(bool maximized) {
  static auto default_display = reinterpret_cast<void*(*)()>(dlsym(RTLD_DEFAULT, "gdk_display_get_default"));
  static auto xdisplay = reinterpret_cast<Display*(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_x11_display_get_xdisplay"));
  static auto xid = reinterpret_cast<unsigned long(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_x11_window_get_xid"));
  if (!default_display || !xdisplay || !xid) throw std::runtime_error("Cannot prepare the initial X11 window");
  auto display = xdisplay(default_display());
  // Keep the WM from mapping the new SWELL surface before its background is ready.
  XGrabServer(display);
  struct Unlock { Display* display; ~Unlock() { XUngrabServer(display); XFlush(display); } } unlock{display};
  ShowWindow(window_, maximized ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
  if (auto native = SWELL_GetOSWindow(window_, "GdkWindow")) {
    const auto target = xid(native);
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, target, &attributes) && !startup_pixels_.empty()) {
      auto pixmap = XCreatePixmap(display, target, attributes.width, attributes.height, attributes.depth);
      auto surface = cairo_xlib_surface_create(display, pixmap, attributes.visual, attributes.width, attributes.height);
      auto frame = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(startup_pixels_.data()),
        CAIRO_FORMAT_RGB24, startup_width_, startup_height_, startup_width_ * 4);
      auto dc = cairo_create(surface);
      cairo_scale(dc, double(attributes.width) / startup_width_, double(attributes.height) / startup_height_);
      cairo_set_source_surface(dc, frame, 0, 0); cairo_paint(dc);
      cairo_destroy(dc); cairo_surface_destroy(frame); cairo_surface_destroy(surface);
      XSetWindowBackgroundPixmap(display, target, pixmap); XClearWindow(display, target); XFreePixmap(display, pixmap);
    }
  }
  UpdateWindow(window_);
}
void SwellWindow::clear_startup_frame() {
  static auto get_display = reinterpret_cast<void*(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_window_get_display"));
  static auto xdisplay = reinterpret_cast<Display*(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_x11_display_get_xdisplay"));
  static auto xid = reinterpret_cast<unsigned long(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_x11_window_get_xid"));
  if (auto native = SWELL_GetOSWindow(window_, "GdkWindow"); native && get_display && xdisplay && xid) {
    auto display = xdisplay(get_display(native));
    XSetWindowBackgroundPixmap(display, xid(native), None); XFlush(display);
  }
  startup_pixels_.clear(); startup_pixels_.shrink_to_fit();
}
#endif
void SwellWindow::set_background(unsigned color) {
  if (background_ == color) return;
  background_ = color;
  if (window_) InvalidateRect(window_, nullptr, FALSE);
}
void SwellWindow::prepare_dock() { GetWindowRect(window_, &floating_); }
void SwellWindow::restore_floating(bool show) {
  SetParent(window_, nullptr);
  SetWindowLong(window_, GWL_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner_));
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
