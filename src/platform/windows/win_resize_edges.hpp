#pragma once
#include <windows.h>
#include <algorithm>

namespace reaweb {
// A paint-free input ring keeps Chromium's child HWNDs out of native resize gestures.
class WinResizeEdges {
  HWND window_ = nullptr;
  int width_ = 0, height_ = 0, edge_ = 0;
  static int hit(HWND window, LPARAM position) {
    RECT r{}; GetWindowRect(window, &r);
    const int x = static_cast<short>(LOWORD(position)), y = static_cast<short>(HIWORD(position));
    const int edge = std::max(4, MulDiv(6, GetDpiForWindow(window), 96));
    const bool left = x < r.left + edge, right = x >= r.right - edge;
    const bool top = y < r.top + edge, bottom = y >= r.bottom - edge;
    return top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) :
      bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) : left ? HTLEFT : right ? HTRIGHT : HTCLIENT;
  }
  static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST) return hit(window, lp);
    if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) {
      SendMessageW(GetParent(window), message, wp, lp); return 0;
    }
    if (message == WM_SETCURSOR) {
      const int edge = LOWORD(lp);
      SetCursor(LoadCursorW(nullptr, edge == HTTOPLEFT || edge == HTBOTTOMRIGHT ? IDC_SIZENWSE :
        edge == HTTOPRIGHT || edge == HTBOTTOMLEFT ? IDC_SIZENESW : edge == HTLEFT || edge == HTRIGHT ? IDC_SIZEWE : IDC_SIZENS));
      return TRUE;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) { PAINTSTRUCT paint{}; BeginPaint(window, &paint); EndPaint(window, &paint); return 0; }
    return DefWindowProcW(window, message, wp, lp);
  }
public:
  void sync(HWND owner, bool active) {
    if (!active) { if (window_) ShowWindow(window_, SW_HIDE); return; }
    if (!window_) {
      WNDCLASSW type{}; type.lpfnWndProc = proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"ReaWebAPI.ResizeEdges";
      RegisterClassW(&type);
      window_ = CreateWindowExW(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, type.lpszClassName, L"", WS_CHILD,
        0, 0, 0, 0, owner, nullptr, type.hInstance, nullptr);
      if (!window_) return;
    }
    RECT rect{}; GetClientRect(owner, &rect);
    const int edge = std::max(4, MulDiv(6, GetDpiForWindow(owner), 96));
    if (rect.right != width_ || rect.bottom != height_ || edge != edge_) {
      width_ = rect.right; height_ = rect.bottom; edge_ = edge;
      auto outer = CreateRectRgn(0, 0, width_, height_);
      auto inner = CreateRectRgn(edge, edge, std::max(edge, width_ - edge), std::max(edge, height_ - edge));
      CombineRgn(outer, outer, inner, RGN_DIFF); DeleteObject(inner);
      if (!SetWindowRgn(window_, outer, FALSE)) DeleteObject(outer);
      SetWindowPos(window_, HWND_TOP, 0, 0, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (!IsWindowVisible(window_) || GetWindow(window_, GW_HWNDPREV))
      SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
};
}
