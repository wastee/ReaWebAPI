#pragma once
#include "platform/shared/swell_window.hpp"
#include <dlfcn.h>

namespace reaweb {
// Use REAPER's GDK connection. Linking another GTK into the host is unsupported.
class LinuxAlpha {
  template<class T> static T symbol(const char* name) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name)); }
  struct Color { double r, g, b, a; };
  void* native_ = nullptr;
  void* clock_ = nullptr;
  unsigned long signal_ = 0;
  bool established_ = false, pending_ = false;
  static void clear(void*, void* data) {
    auto self = static_cast<LinuxAlpha*>(data);
    if (!self->native_) return;
    auto dc = symbol<void*(*)(void*)>("gdk_cairo_create")(self->native_);
    if (!dc) return;
    symbol<void(*)(void*, int)>("cairo_set_operator")(dc, 0); // CAIRO_OPERATOR_CLEAR
    symbol<void(*)(void*)>("cairo_paint")(dc);
    symbol<void(*)(void*)>("cairo_destroy")(dc);
  }
public:
  static std::string unavailable() {
    for (const auto name : {"gdk_screen_get_default", "gdk_screen_get_rgba_visual", "gdk_screen_is_composited",
         "gdk_visual_get_depth", "gdk_window_get_visual", "gdk_window_get_frame_clock", "gdk_window_set_opacity",
         "gdk_window_set_background_rgba", "gdk_cairo_create", "cairo_set_operator", "cairo_paint", "cairo_destroy",
         "g_signal_connect_data", "g_signal_handler_disconnect", "g_object_ref", "g_object_unref",
         "gdk_display_get_default", "gdk_x11_display_get_xdisplay", "gdk_x11_window_get_xid", "XGrabServer", "XUngrabServer", "XFlush"})
      if (!dlsym(RTLD_DEFAULT, name)) return std::string("Desktop Alpha requires GDK3/X11: missing ") + name;
    auto screen = symbol<void*(*)()>("gdk_screen_get_default")();
    if (!screen || !symbol<int(*)(void*)>("gdk_screen_is_composited")(screen)) return "No X11 compositing manager is active";
    auto visual = symbol<void*(*)(void*)>("gdk_screen_get_rgba_visual")(screen);
    if (!visual || symbol<int(*)(void*)>("gdk_visual_get_depth")(visual) != 32) return "No 32-bit RGBA visual is available";
    return {};
  }
  ~LinuxAlpha() { detach(); }
  void detach() {
    if (clock_ && signal_) symbol<void(*)(void*, unsigned long)>("g_signal_handler_disconnect")(clock_, signal_);
    if (clock_) symbol<void(*)(void*)>("g_object_unref")(clock_);
    clock_ = native_ = nullptr; signal_ = 0; pending_ = false;
  }
  void show(SwellWindow& window) {
    const auto reason = unavailable();
    if (!reason.empty()) throw Error("TRANSPARENCY_UNAVAILABLE", reason);
    auto handle = static_cast<HWND>(window.handle());
    if (native_) { window.set_visible(true); return; }
    auto display = symbol<void*(*)()>("gdk_display_get_default")();
    auto xdisplay = symbol<void*(*)(void*)>("gdk_x11_display_get_xdisplay")(display);
    // Make the initial map and its presentation barrier atomic for the compositor.
    symbol<int(*)(void*)>("XGrabServer")(xdisplay);
    const auto release = [&] {
      symbol<int(*)(void*)>("XUngrabServer")(xdisplay);
      symbol<int(*)(void*)>("XFlush")(xdisplay);
    };
    try {
      window.set_visible(true);
      native_ = SWELL_GetOSWindow(handle, "GdkWindow");
      if (!native_) throw Error("TRANSPARENCY_FAILED", "SWELL did not create an X11 host");
      symbol<void(*)(void*, double)>("gdk_window_set_opacity")(native_, 0);
      auto visual = symbol<void*(*)(void*)>("gdk_window_get_visual")(native_);
      if (!visual || symbol<int(*)(void*)>("gdk_visual_get_depth")(visual) != 32)
        throw Error("TRANSPARENCY_UNAVAILABLE", "This REAPER/SWELL host does not support SWELLGdkAlphaChannel");
      const Color clear_color{0, 0, 0, 0};
      symbol<void(*)(void*, const Color*)>("gdk_window_set_background_rgba")(native_, &clear_color);
      clock_ = symbol<void*(*)(void*)>("gdk_window_get_frame_clock")(native_);
      if (!clock_) throw Error("TRANSPARENCY_FAILED", "The GDK frame clock is unavailable");
      symbol<void*(*)(void*)>("g_object_ref")(clock_);
      signal_ = symbol<unsigned long(*)(void*, const char*, void(*)(void*, void*), void*, void*, int)>("g_signal_connect_data")(
        clock_, "after-paint", clear, this, nullptr, 1);
      if (!signal_) throw Error("TRANSPARENCY_FAILED", "Cannot install the transparent host paint callback");
      clear(nullptr, this);
      pending_ = true;
      release();
    } catch (...) { detach(); window.set_visible(false); release(); throw; }
  }
  unsigned long xid() const { return native_ ? symbol<unsigned long(*)(void*)>("gdk_x11_window_get_xid")(native_) : 0; }
  void present(unsigned long parent) {
    if (!pending_ || parent != xid()) return;
    clear(nullptr, this);
    symbol<void(*)(void*, double)>("gdk_window_set_opacity")(native_, 1);
    established_ = true; pending_ = false;
  }
  bool active() const { return established_; }
  bool pending() const { return pending_; }
};
}
