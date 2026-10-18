#include "../src/platform/windows/platform_win.cpp"
#include <httplib.h>
#include <fstream>
#include <iostream>
#include <thread>

using namespace reaweb;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)

static void check_floating_frame(HWND hwnd) {
  CHECK(!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD));
  CHECK((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
  BOOL rendered = FALSE;
  CHECK(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &rendered, sizeof(rendered))));
  CHECK(rendered);
  auto region = CreateRectRgn(0, 0, 0, 0);
  const auto type = GetWindowRgn(hwnd, region); DeleteObject(region);
  CHECK(type == ERROR);
}

int main(int argc, char** argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    const auto root = fs::current_path() / ".cache" / ("startup-win-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    httplib::Server server;
    server.Get("/", [](const auto&, auto& response) {
      response.set_content("<!doctype html><link rel=stylesheet href=/style.css><div id=marker></div>", "text/html");
    });
    server.Get("/style.css", [](const auto&, auto& response) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      response.set_content("html{background:#182838}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}", "text/css");
    });
    const int port = server.bind_to_any_port("127.0.0.1"); CHECK(port > 0);
    std::thread serving([&] { server.listen_after_bind(); });
    struct Stop { httplib::Server& server; std::thread& thread; ~Stop() { server.stop(); thread.join(); } } stop{server, serving};
    auto host = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"Startup test host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      0, 0, 1000, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    struct Destroy { HWND hwnd; ~Destroy() { DestroyWindow(hwnd); } } destroy{host};
    ShowWindow(host, SW_SHOW); SetForegroundWindow(host);
    const int count = argc > 1 ? std::stoi(argv[1]) : 20;
    if (argc > 2 && std::string(argv[2]) == "--hidden") {
      if (GetForegroundWindow() != host) {
        POINT saved{}; GetCursorPos(&saved); SetCursorPos(50, 100);
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_MOUSE;
        input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        CHECK(SendInput(2, input, sizeof(INPUT)) == 2);
        const auto start = GetTickCount64();
        while (GetForegroundWindow() != host && GetTickCount64()-start < 1000) {
          MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
          Sleep(1);
        }
        SetCursorPos(saved.x, saved.y);
      }
      CHECK(GetForegroundWindow() == host);
      for (bool frameless : {false, true}) for (bool focus_first : {false, true}) for (int run = 0; run < count; ++run) {
        const auto path = root / ("hidden-" + std::to_string(frameless) + "-" + std::to_string(focus_first) + "-" + std::to_string(run)); fs::create_directories(path);
        const auto entry = path / "index.html";
        std::ofstream(entry) << "<!doctype html><style>html{background:#182838}body{margin:0}"
          "#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>";
        auto platform = make_platform(path / "profile");
        bool ready = false;
        std::string error;
        WindowOptions options;
        options.entry = entry; options.title = "Hidden acceptance"; options.parent = host; options.hidden = true;
        options.frameless = frameless;
        options.script = "addEventListener('DOMContentLoaded',()=>chrome.webview.postMessage('ui-ready'));";
        options.on_message = [&](std::string message) { if (message == "ui-ready") ready = true; };
        options.on_error = [&](std::string message) { error = std::move(message); };
        SetForegroundWindow(host); SetFocus(host);
        auto window = platform->open(std::move(options));
        const auto hwnd = static_cast<HWND>(window->native_handle());
        SetWindowPos(hwnd, HWND_TOPMOST, 100, 100, 600, 420, SWP_NOACTIVATE);
        const auto pump = [&] {
          MSG msg{};
          while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
          platform->pump(); window->tick();
          CHECK(error.empty());
        };
        const auto painted = [&] {
          const auto deadline = GetTickCount64() + 5000;
          do {
            pump(); DwmFlush();
            POINT origin{}; ClientToScreen(hwnd, &origin);
            const auto dc = GetDC(nullptr);
            const auto color = GetPixel(dc, origin.x + 40, origin.y + 40);
            ReleaseDC(nullptr, dc);
            if (GetGValue(color) > 180 && int(GetGValue(color)) - GetRValue(color) > 70 &&
                int(GetGValue(color)) - GetBValue(color) > 70) return;
            Sleep(2);
          } while (GetTickCount64() < deadline);
          throw std::runtime_error("Hidden window did not paint page content, frameless=" + std::to_string(frameless));
        };
        const auto started = GetTickCount64();
        while (!ready || GetTickCount64() - started < 1000) {
          pump(); CHECK(!IsWindowVisible(hwnd) && !window->visible());
          CHECK(GetForegroundWindow() == host);
          CHECK(GetTickCount64() - started < 20000); Sleep(2);
        }
        if (focus_first) window->focus(); else window->show();
        CHECK(window->visible() && GetForegroundWindow() == (focus_first ? hwnd : host));
        painted(); CHECK(GetForegroundWindow() == (focus_first ? hwnd : host));
        window->focus(); CHECK(window->visible() && GetForegroundWindow() == hwnd);
        painted();
        for (int cycle = 0; cycle < 3; ++cycle) {
          window->set_visible(false); pump(); CHECK(!window->visible());
          SetWindowPos(hwnd, nullptr, 0, 0, 600 + cycle * 40, 420 + cycle * 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
          if (cycle == 1) window->focus(); else window->show();
          CHECK(window->visible()); painted();
          SetWindowPos(hwnd, nullptr, 0, 0, 640 + cycle * 40, 450 + cycle * 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
          painted();
        }
        window->set_visible(false); window->cancel_pending_show(); window->show(); pump(); CHECK(!window->visible());
        window.reset(); platform.reset(); CHECK(!IsWindow(hwnd));
      }
      for (bool frameless : {false, true}) for (int intent = 0; intent < 3; ++intent) {
        const auto path = root / ("early-" + std::to_string(frameless) + "-" + std::to_string(intent)); fs::create_directories(path);
        const auto entry = path / "index.html"; std::ofstream(entry) << "<!doctype html><p>Early intent</p>";
        auto platform = make_platform(path / "profile");
        bool ready = false;
        WindowOptions options; options.entry = entry; options.hidden = true; options.parent = host;
        options.frameless = frameless;
        options.script = "window.chrome.webview.postMessage('ready');";
        options.on_message = [&](std::string message) { ready |= message == "ready"; };
        options.on_error = [](std::string message) { throw std::runtime_error(message); };
        SetForegroundWindow(host); SetFocus(host);
        auto window = platform->open(std::move(options));
        const auto hwnd = static_cast<HWND>(window->native_handle());
        if (intent == 2) { window->cancel_pending_show(); window.reset(); }
        else {
          if (intent == 0) { window->show(); CHECK(GetForegroundWindow() == host); }
          else window->focus();
          CHECK(window->visible()); window->set_visible(false);
        }
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < 1000 || (intent != 2 && !ready)) {
          MSG msg{};
          while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
          platform->pump(); if (window) window->tick();
          CHECK(!IsWindowVisible(hwnd) && GetForegroundWindow() != hwnd);
          CHECK(GetTickCount64() - started < 20000); Sleep(2);
        }
      }
      std::cout << "PASS: framed/frameless hidden creation, JS preloading, first Show/Focus pixels, repeated Hide/Show, resize and close, cold profiles=" << count * 4 << ", early-intent races=6\n";
      return 0;
    }
    const bool undock_pending = argc > 2 && std::string(argv[2]) == "--undock-pending";
    const bool initially_docked = undock_pending || (argc > 2 && std::string(argv[2]) == "--dock");
    int captures = 0;
    for (int variant = 0; variant < 5; ++variant) for (int run = 0; run < count; ++run) {
      const auto path = root / (std::to_string(variant) + "-" + std::to_string(run)); fs::create_directories(path);
      const auto entry = path / "index.html";
      const auto background = variant == 1 ? "#e8d8c8" : variant == 2 ? "linear-gradient(120deg,#182838,#486878)" : "#182838";
      std::ofstream(entry) << "<!doctype html><style>html{min-height:100%;background:" << background
        << "}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>";
      auto platform = make_platform(path / "profile");
      std::string error;
      WindowOptions options;
      options.entry = entry; options.title = "Startup acceptance"; options.parent = host;
      options.frameless = variant == 4;
      if (variant == 3) options.url = "http://127.0.0.1:" + std::to_string(port) + "/";
      options.on_message = [](std::string) {};
      options.on_error = [&](std::string message) { std::cerr << message << '\n'; error = std::move(message); };
      auto window = platform->open(std::move(options));
      const auto hwnd = static_cast<HWND>(window->native_handle());
      bool docked = initially_docked;
      const auto dock = [&] {
        window->prepare_dock();
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE); SetParent(hwnd, host);
      };
      if (docked) dock();
      SetWindowPos(hwnd, HWND_TOPMOST, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
      CHECK(!window->visible()); window->show_after_create(); CHECK(!window->visible());
      DWORD cloaked = 0;
      if (!docked) CHECK(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked);
      else {
        auto region = CreateRectRgn(0,0,0,0); const auto type = GetWindowRgn(hwnd,region); DeleteObject(region);
        CHECK(type == NULLREGION);
      }
      if (undock_pending) {
        window->restore_floating(); docked = false;
        CHECK(window->presentation_pending()); CHECK(!window->visible());
        auto region = CreateRectRgn(0, 0, 0, 0);
        const auto type = GetWindowRgn(hwnd, region); DeleteObject(region);
        CHECK(type == NULLREGION);
      }
      const auto started = std::chrono::steady_clock::now();
      int samples = 0;
      while (samples < 20 && error.empty()) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        window->tick();
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(20));
        if (window->visible()) {
          DwmFlush();
          POINT origin{}; ClientToScreen(hwnd, &origin);
          const auto seen = GetAncestor(WindowFromPoint({origin.x+40,origin.y+40}), GA_ROOT);
          if (seen != (docked ? host : hwnd)) std::cerr << "sample root=" << seen << " target=" << hwnd
            << " host=" << host << " parent=" << GetParent(hwnd) << " style=" << std::hex << GetWindowLongPtrW(hwnd,GWL_STYLE) << std::dec << '\n';
          if (seen != (docked ? host : hwnd)) {
            DWORD child_cloak=0, host_cloak=0;
            DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&child_cloak,sizeof(child_cloak));
            DwmGetWindowAttribute(host,DWMWA_CLOAKED,&host_cloak,sizeof(host_cloak));
            RECT hr{}; GetWindowRect(host,&hr);
            std::cerr << "origin=" << origin.x << ',' << origin.y << " hostrect=" << hr.left << ',' << hr.top << ',' << hr.right << ',' << hr.bottom
              << " cloak=" << child_cloak << ',' << host_cloak << " hostvisible=" << IsWindowVisible(host) << '\n';
          }
          CHECK(seen == (docked ? host : hwnd));
          const auto dc = GetDC(nullptr);
          const auto marker = GetPixel(dc, origin.x+40, origin.y+40);
          const auto color = GetPixel(dc, origin.x+160, origin.y+160);
          ReleaseDC(nullptr, dc);
          // Display color management can alter the exact RGB values.
          CHECK(GetGValue(marker) > 180 && int(GetGValue(marker))-GetRValue(marker) > 70 && int(GetGValue(marker))-GetBValue(marker) > 70);
          CHECK(!(GetRValue(color) > 245 && GetGValue(color) > 245 && GetBValue(color) > 245));
          ++samples; ++captures;
        }
        Sleep(1);
      }
      CHECK(error.empty());
      if (docked) window->restore_floating();
      if (variant != 4) check_floating_frame(hwnd);
      for (int cycle = 0; cycle < 3; ++cycle) {
        dock(); window->tick();
        window->restore_floating(); window->tick();
        if (variant != 4) check_floating_frame(hwnd);
      }
      std::cout << "variant=" << variant << " run=" << run << " samples=" << samples << '\n';
    }
    std::cout << "PASS: native first-visible frames and dock/float frame restoration, cold profiles=" << count*5 << " captures=" << captures << '\n';
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
