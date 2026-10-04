#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <string>

#include "../src/platform/windows/platform_win.cpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace reaweb;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed at " + std::to_string(__LINE__) + ": " #value); } while (false)

std::function<void()> tick_windows;

void pump(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  do {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (tick_windows) tick_windows();
    if (done()) return;
    Sleep(5);
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("Cursor test timed out");
}
void settle() {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  pump([&] { return std::chrono::steady_clock::now() >= until; });
}
CURSORINFO cursor() {
  CURSORINFO info{sizeof(info)};
  CHECK(GetCursorInfo(&info));
  return info;
}
#define visible() CHECK(cursor().flags & CURSOR_SHOWING)
void activate(HWND hwnd) {
  const auto current = GetCurrentThreadId(), foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  const bool attached = current != foreground && AttachThreadInput(current, foreground, TRUE);
  SetForegroundWindow(hwnd);
  if (attached) AttachThreadInput(current, foreground, FALSE);
  CHECK(GetForegroundWindow() == hwnd);
}
LRESULT CALLBACK native_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  // Model a host that retains its tool cursor instead of setting it on every move.
  if (message == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) return TRUE;
  return DefWindowProcW(hwnd, message, wp, lp);
}

struct Desktop {
  BOOL vanish = FALSE;
  POINT position{};
  HWND foreground = GetForegroundWindow(), host = nullptr;
  Desktop() {
    CHECK(SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &vanish, 0));
    CHECK(GetCursorPos(&position));
  }
  ~Desktop() {
    if (host) DestroyWindow(host);
    SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, reinterpret_cast<void*>(static_cast<INT_PTR>(vanish)), SPIF_SENDCHANGE);
    SetCursorPos(position.x, position.y);
    if (IsWindow(foreground)) SetForegroundWindow(foreground);
  }
};

int main(int argc, char** argv) {
  try {
    const bool srgb = argc > 1 && std::string(argv[1]) == "--srgb";
    const bool vanish_off = argc > 1 && std::string(argv[1]) == "--vanish-off";
    startup_color_profile = srgb ? ColorProfile::SRGB : ColorProfile::Default;
    Desktop desktop;
    CHECK(SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, reinterpret_cast<void*>(static_cast<INT_PTR>(!vanish_off)), SPIF_SENDCHANGE));
    const auto root = fs::current_path() / ("cursor-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    const auto entry = root / "index.html";
    std::ofstream(entry) << "<!doctype html><title>Cursor regression</title><input id='text' autofocus>"
      "<div id='none' style='position:absolute;left:20px;top:100px;width:200px;height:100px;cursor:none'>hidden cursor</div>";
    WNDCLASSW cls{}; cls.lpfnWndProc = native_proc; cls.lpszClassName = L"ReaWebAPI.CursorTest";
    cls.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    CHECK(RegisterClassW(&cls));
    desktop.host = CreateWindowW(cls.lpszClassName, L"Native host cursor regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      30, 30, 1100, 680, nullptr, nullptr, nullptr, nullptr);
    CHECK(desktop.host);
    auto platform = make_platform(root / "profile");
    int ready = 0, inputs = 0, mouse_events = 0;
    std::string error;
    WindowOptions options;
    options.entry = entry; options.parent = desktop.host; options.title = "Cursor regression";
    options.script = "addEventListener('DOMContentLoaded',()=>chrome.webview.postMessage('ready'));"
      "addEventListener('input',()=>chrome.webview.postMessage('input'));"
      "for(const name of ['mousemove','pointermove','click']) addEventListener(name,()=>chrome.webview.postMessage('mouse'));";
    options.on_message = [&](std::string value) { if (value == "ready") ++ready; if (value == "input") ++inputs; if (value == "mouse") ++mouse_events; };
    options.on_error = [&](std::string value) { error = value; };
    auto first = platform->open(options), second = platform->open(options);
    tick_windows = [&] { if (first) first->tick(); if (second) second->tick(); };
    pump([&] { return ready == 2 || !error.empty(); });
    CHECK(error.empty());
    second->set_visible(false);
    auto hwnd = static_cast<HWND>(first->native_handle());
    SetWindowPos(hwnd, nullptr, 450, 60, 600, 500, SWP_NOZORDER);
    activate(hwnd); first->focus(); settle();
    first->evaluate("document.getElementById('text').focus()"); settle();
    std::cout << first->diagnostics().dump() << '\n';
    auto type = [&](bool hide = true) {
      auto target = GetFocus();
      CHECK(target && target != hwnd && IsChild(hwnd, target));
      const auto before = inputs;
      CHECK(GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT));
      INPUT keys[2]{};
      for (auto& key : keys) { key.type = INPUT_KEYBOARD; key.ki.wVk = 'A'; }
      keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
      const auto sent = SendInput(2, keys, sizeof(INPUT));
      if (sent == 1) SendInput(1, keys + 1, sizeof(INPUT));
      CHECK(sent == 2);
      pump([&] { return inputs > before || !error.empty(); });
      CHECK(error.empty()); settle();
      if (static_cast<bool>(cursor().flags & CURSOR_SHOWING) == hide) std::cerr << "input=" << inputs << ", expected hidden=" << hide << ", cursor=" << cursor().hCursor << std::endl;
      CHECK(static_cast<bool>(cursor().flags & CURSOR_SHOWING) != hide);
      CHECK(GetFocus() == target);
    };
    auto move_native = [&](int x) {
      POINT point{x, 160}; ClientToScreen(desktop.host, &point);
      CHECK(SetCursorPos(point.x, point.y)); settle();
      CHECK(WindowFromPoint(point) == desktop.host);
      INPUT movement{}; movement.type = INPUT_MOUSE; movement.mi.dx = 1; movement.mi.dwFlags = MOUSEEVENTF_MOVE;
      CHECK(SendInput(1, &movement, sizeof(movement)) == 1); settle();
    };
    SetCursor(LoadCursorW(nullptr, IDC_CROSS)); move_native(80); visible();
    if (vanish_off) {
      type(false); move_native(100); visible();
      CHECK(mouse_events == 0);
      tick_windows = {};
      std::cout << "PASS: system mouse-vanish disabled at browser startup\n";
      return 0;
    }
    type();
    const bool hidden_after_typing = !(cursor().flags & CURSOR_SHOWING);
    move_native(100);
    const bool hidden_after_move = !(cursor().flags & CURSOR_SHOWING);
    std::cout << "hidden after typing=" << hidden_after_typing << ", after native movement=" << hidden_after_move << '\n';
    CHECK(hidden_after_typing && !hidden_after_move);
    CHECK(mouse_events == 0);
    CHECK(GetCursor() == LoadCursorW(nullptr, IDC_CROSS));
    for (int i = 0; i < 3; ++i) { type(); move_native(110 + i * 10); visible(); }
    INPUT navigation[2]{};
    for (auto& key : navigation) { key.type = INPUT_KEYBOARD; key.ki.wVk = VK_LEFT; }
    navigation[1].ki.dwFlags = KEYEVENTF_KEYUP;
    CHECK(SendInput(2, navigation, sizeof(INPUT)) == 2); settle();
    std::cout << "hidden after caret navigation=" << !(cursor().flags & CURSOR_SHOWING) << '\n';
    move_native(145); visible();
    first->prepare_dock(); SetParent(hwnd, desktop.host);
    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
    SetWindowPos(hwnd, nullptr, 420, 10, 600, 500, SWP_FRAMECHANGED | SWP_NOZORDER);
    activate(desktop.host); first->focus(); settle();
    first->evaluate("document.getElementById('text').focus()"); settle();
    SetCursor(LoadCursorW(nullptr, IDC_CROSS)); settle(); type(); move_native(150); visible();
    CHECK(GetCursor() == LoadCursorW(nullptr, IDC_CROSS));
    first->restore_floating(); settle(); type(); move_native(160); visible();
    first->reload(); pump([&] { return ready == 3 || !error.empty(); }); CHECK(error.empty());
    first->focus(); settle(); type(); move_native(170); visible();
    POINT hidden{80, 150}; ClientToScreen(hwnd, &hidden);
    CHECK(SetCursorPos(hidden.x, hidden.y)); settle();
    CHECK(!(cursor().flags & CURSOR_SHOWING)); // CSS cursor:none remains effective.
    move_native(180); SetCursor(LoadCursorW(nullptr, IDC_CROSS)); visible();
    type(); first->set_visible(false); settle(); visible();
    first->set_visible(true); activate(hwnd); first->focus(); settle();
    type(); first.reset(); settle(); visible(); move_native(190); visible();
    first = platform->open(options); hwnd = static_cast<HWND>(first->native_handle());
    pump([&] { return ready == 4 || !error.empty(); }); CHECK(error.empty());
    SetWindowPos(hwnd, nullptr, 450, 60, 600, 500, SWP_NOZORDER);
    activate(hwnd); first->focus(); settle();
    type(); move_native(200); visible();
    SetCapture(desktop.host);
    CHECK(ShowCursor(FALSE) < 0);
    move_native(210); CHECK(!(cursor().flags & CURSOR_SHOWING));
    CHECK(ShowCursor(TRUE) >= 0); ReleaseCapture(); settle(); visible();
    tick_windows = {};
    std::cout << "PASS: hide while typing / restore on native movement, native cursor shape, dock/undock, reload, close/reopen, CSS cursor:none and native capture ("
      << (srgb ? "sRGB" : "Default") << ")\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
