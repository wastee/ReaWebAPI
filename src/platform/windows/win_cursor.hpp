#pragma once
#include <windows.h>
#include <cwchar>

namespace reaweb {
// A windowed WebView shares the host's input queue, but does not receive mouse
// movement delivered to REAPER. Let Chromium balance its own cursor visibility.
class WinCursor {
  HWND owner_ = nullptr, browser_ = nullptr, native_ = nullptr;
  HWND restoring_ = nullptr;
  HCURSOR cursor_ = nullptr, restore_cursor_ = nullptr;
  POINT point_{};
  bool sampled_ = false, typing_ = false, hidden_ = false;

  HWND native_at(POINT point) const {
    auto window = WindowFromPoint(point);
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        (window != owner_ && IsChild(owner_, window))) return nullptr;
    return window;
  }
  static bool dragging() {
    return GetCapture() || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
      (GetAsyncKeyState(VK_RBUTTON) & 0x8000) || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ||
      (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) || (GetAsyncKeyState(VK_XBUTTON2) & 0x8000);
  }
  void sync_cursor() {
    if (!restoring_) return;
    CURSORINFO info{sizeof(info)};
    if (!GetCursorInfo(&info)) return;
    auto native = restoring_;
    if (native_at(info.ptScreenPos) != native || dragging()) { restoring_ = nullptr; return; }
    if (restore_cursor_) SetCursor(restore_cursor_);
    const auto hit = SendMessageW(native, WM_NCHITTEST, 0, MAKELPARAM(info.ptScreenPos.x, info.ptScreenPos.y));
    SendMessageW(native, WM_SETCURSOR, reinterpret_cast<WPARAM>(native), MAKELPARAM(hit, WM_MOUSEMOVE));
    info.cbSize = sizeof(info);
    if (GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING)) restoring_ = nullptr;
  }
  void restore(POINT point, HWND native) {
    if (!browser_ || !IsWindow(browser_) || !IsChild(owner_, browser_)) return;
    const auto saved = native == native_ ? cursor_ : GetCursor();
    DWORD_PTR result = 0;
    // Non-client movement resets Chromium's typing state without sending a
    // fictitious move/click into the document or changing keyboard focus.
    if (!SendMessageTimeoutW(browser_, WM_NCMOUSEMOVE, HTNOWHERE, MAKELPARAM(point.x, point.y),
        SMTO_ABORTIFHUNG, 50, &result)) return;
    restoring_ = native; restore_cursor_ = saved;
    typing_ = false;
    // Chromium may apply the visibility change asynchronously.
    sync_cursor();
  }
public:
  static constexpr const char* message = "__reawebTyping";
  static constexpr const char* script = R"JS(
;for (const name of ['keydown', 'beforeinput']) {
  addEventListener(name, event => {
    if (event.isTrusted && !document.pointerLockElement) chrome.webview.postMessage('__reawebTyping');
  }, true);
}
)JS";
  void attach(HWND owner) { owner_ = owner; tick(); }
  void typed() {
    if (dragging()) return;
    auto focus = GetFocus();
    if (!focus || !IsChild(owner_, focus)) return;
    wchar_t name[64]{}; GetClassNameW(focus, name, 64);
    if (wcsncmp(name, L"Chrome_WidgetWin_", 16)) return;
    browser_ = focus;
    restoring_ = nullptr;
    typing_ = true;
    hidden_ = false;
  }
  void tick() {
    if (!owner_) return;
    sync_cursor();
    CURSORINFO info{sizeof(info)};
    if (!GetCursorInfo(&info)) return;
    const bool moved = sampled_ && (point_.x != info.ptScreenPos.x || point_.y != info.ptScreenPos.y);
    point_ = info.ptScreenPos; sampled_ = true;
    auto native = native_at(point_);
    if (typing_ && !info.flags) hidden_ = true;
    if (typing_ && !info.flags && moved && native && !dragging()) restore(point_, native);
    if (info.flags & CURSOR_SHOWING) {
      if (native && !dragging()) { native_ = native; cursor_ = info.hCursor; }
      if (hidden_ || moved) typing_ = false;
    }
  }
  void finish() {
    CURSORINFO info{sizeof(info)};
    if (typing_ && GetCursorInfo(&info) && !info.flags && !dragging())
      if (auto native = native_at(info.ptScreenPos)) restore(info.ptScreenPos, native);
    typing_ = false; browser_ = nullptr;
  }
};
}
