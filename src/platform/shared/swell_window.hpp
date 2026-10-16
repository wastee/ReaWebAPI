#pragma once
#include "platform/platform.hpp"
#include <swell/swell.h>

namespace reaweb {
class SwellWindow {
public:
  SwellWindow(const std::string& title, void* parent, std::function<void()> focus = {}, std::function<void()> close = {},
    bool hidden = false, bool hosted = false);
  ~SwellWindow();
  void* handle() const { return window_; }
  bool closed() const;
  void prepare_dock();
  void restore_floating(bool show = true);
  void focus();
  void set_title(const std::string& title);
  void set_visible(bool visible);
  void reparent_host(void* parent);
  void* host_parent() const { return owner_; }
  bool visible() const;
  bool focused() const;
  Json placement() const;
  void restore_placement(const Json& value);
  void set_background(unsigned color);
  unsigned background_color() const { return background_; }
  void set_transparent(bool value) { transparent_ = value; }
  std::function<void()> resize;
  std::function<void()> before_destroy;
  std::function<void()> configure_floating;
  std::function<void(LPARAM)> context_menu;
private:
  HWND window_ = nullptr;
  HWND owner_ = nullptr;
  HWND hosted_parking_ = nullptr;
  RECT floating_{};
  bool closed_ = false;
  unsigned background_ = 0xffffff;
  bool transparent_ = false;
  std::function<void()> focus_;
  std::function<void()> close_;
  static INT_PTR procedure(HWND, UINT, WPARAM, LPARAM);
};
}
