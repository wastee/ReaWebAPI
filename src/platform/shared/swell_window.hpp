#pragma once
#include "platform/platform.hpp"
#include <swell/swell.h>

namespace reaweb {
class SwellWindow {
public:
  SwellWindow(const std::string& title, void* parent, std::function<void()> focus = {}, std::function<void()> close = {},
    bool hidden = false);
  ~SwellWindow();
  void* handle() const { return window_; }
  bool closed() const;
  void prepare_dock();
  void restore_floating();
  void focus();
  void set_title(const std::string& title);
  void set_visible(bool visible);
  bool visible() const;
  bool focused() const;
  Json placement() const;
  void restore_placement(const Json& value);
  void set_background(unsigned color);
  std::function<void()> resize;
  std::function<void(LPARAM)> context_menu;
private:
  HWND window_ = nullptr;
  HWND owner_ = nullptr;
  RECT floating_{};
  bool closed_ = false;
  unsigned background_ = 0xffffff;
  std::function<void()> focus_;
  std::function<void()> close_;
  static INT_PTR procedure(HWND, UINT, WPARAM, LPARAM);
};
}
