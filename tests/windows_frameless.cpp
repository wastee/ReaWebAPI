// Opt-in native input acceptance. This test moves the pointer and activates its own windows.
#include "../src/platform/windows/platform_win.cpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
using namespace reaweb;
#define CHECK(v) do { if (!(v)) throw std::runtime_error(#v); } while (false)
int main() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    auto path = fs::current_path() / ".cache" / ("frameless-win-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(path);
    std::ofstream(path / "index.html") << R"HTML(<!doctype html><style>html{background:#182838;color:white}body{margin:0}header{height:60px;padding:16px;box-sizing:border-box}button{float:right}</style><header data-reaweb-drag>Native title <button onclick="chrome.webview.postMessage('button')">Button</button></header><input value="Editable"><p>Native resize fixture</p>)HTML";
    auto platform = make_platform(path / "profile");
    bool docked = false;
    for (bool resizable : {true, false}) {
      std::string error; int clicks = 0;
      WindowOptions options; options.entry = path / "index.html"; options.title = "Frameless acceptance";
      options.hidden = true; options.frameless = true; options.resizable = resizable;
      options.is_docked = [&] { return docked; };
      options.on_error = [&](std::string message) { error = message; };
      options.on_message = [&](std::string message) { if (message == "button") ++clicks; else std::cout << message << std::endl; };
      options.script = "window.reaper={events:{on(){}},window:{getState:()=>Promise.resolve({framelessActive:true})}};window.documentMarker=731;";
      auto window = platform->open(std::move(options)); window->show_after_create();
      auto hwnd = static_cast<HWND>(window->native_handle());
      const auto pump = [&](unsigned duration) {
        const auto start = GetTickCount64();
        do {
          MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
          platform->pump(); window->tick(); CHECK(error.empty()); Sleep(2);
        } while (GetTickCount64() - start < duration);
      };
      pump(2000); CHECK(!IsWindowVisible(hwnd)); CHECK(window->frameless_active());
      RECT initial_client{}, initial_outer{}; GetClientRect(hwnd, &initial_client); GetWindowRect(hwnd, &initial_outer);
      CHECK(initial_client.right == initial_outer.right - initial_outer.left - (resizable ? 12 : 0));
      CHECK(initial_client.bottom == initial_outer.bottom - initial_outer.top - (resizable ? 12 : 0));
      window->restore_placement({{"x", 200}, {"y", 180}, {"width", 600}, {"height", 400}});
      window->show(); window->focus(); pump(600); CHECK(window->visible());
      RECT client{}, outer{}; GetClientRect(hwnd, &client); GetWindowRect(hwnd, &outer);
      CHECK(client.right == outer.right - outer.left - (resizable ? 12 : 0) && client.bottom == outer.bottom - outer.top - (resizable ? 12 : 0));
      const auto gesture = [&](int x, int y, int dx, int dy) {
        std::atomic<bool> done{false};
        std::thread input([&] {
          SetCursorPos(x, y); Sleep(80);
          INPUT event{}; event.type = INPUT_MOUSE; event.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; SendInput(1, &event, sizeof(event));
          Sleep(100);
          for (int n = 1; n <= 10; ++n) { SetCursorPos(x + dx * n / 10, y + dy * n / 10); Sleep(20); }
          event.mi.dwFlags = MOUSEEVENTF_LEFTUP; SendInput(1, &event, sizeof(event)); done = true;
        });
        while (!done) pump(20);
        input.join(); pump(250);
      };
      gesture(outer.left + 150, outer.top + 28, 60, 35);
      RECT moved{}; GetWindowRect(hwnd, &moved);
      CHECK(moved.left - outer.left == 60 && moved.top - outer.top == 35);
      gesture(moved.right - (resizable ? 12 : 6) - 40, moved.top + 28, 0, 0);
      CHECK(clicks == 1);
      RECT unchanged{}; GetWindowRect(hwnd, &unchanged); CHECK(EqualRect(&moved, &unchanged));
      std::cout << "PASS: native title drag and button exclusion, resizable=" << resizable << '\n';
      for (const auto& edge : std::vector<POINT>{{0,1},{2,1},{1,0},{1,2},{0,0},{2,0},{0,2},{2,2}}) {
        window->restore_placement({{"x", 200}, {"y", 180}, {"width", 600}, {"height", 400}}); pump(100);
        GetWindowRect(hwnd, &outer);
        const int x = edge.x == 0 ? outer.left + 2 : edge.x == 2 ? outer.right - 2 : (outer.left + outer.right) / 2;
        const int y = edge.y == 0 ? outer.top + 2 : edge.y == 2 ? outer.bottom - 2 : (outer.top + outer.bottom) / 2;
        gesture(x, y, edge.x == 1 ? 0 : 30, edge.y == 1 ? 0 : 30);
        GetWindowRect(hwnd, &moved);
        const bool changed = moved.right - moved.left != 600 || moved.bottom - moved.top != 400;
        CHECK(changed == resizable);
      }
      std::cout << "PASS: all eight resize edges, resizable=" << resizable << '\n';
      auto host = CreateWindowExW(0, L"STATIC", L"Docker fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 720, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
      const auto host_style = GetWindowLongPtrW(host, GWL_STYLE);
      window->prepare_dock(); docked = true;
      SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN); SetParent(hwnd, host);
      SetWindowPos(hwnd, nullptr, 0, 0, 700, 450, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); pump(300);
      CHECK(!window->frameless_active()); CHECK(GetWindowLongPtrW(host, GWL_STYLE) == host_style);
      docked = false; window->restore_floating(); pump(300); CHECK(window->frameless_active());
      window->evaluate("chrome.webview.postMessage(documentMarker===731?'button':'lost')"); pump(200); CHECK(clicks == 2);
      DestroyWindow(host);
      window->restore_placement({{"x", 200}, {"y", 180}, {"width", 432}, {"height", 321}}); pump(100);
      GetWindowRect(hwnd, &outer); CHECK(outer.right - outer.left == 432);
      window.reset(); CHECK(!IsWindow(hwnd));
    }
    std::cout << "PASS: hidden frameless, native drag/resize, fixed size, Dock reparenting, retained document, programmatic size and close\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
