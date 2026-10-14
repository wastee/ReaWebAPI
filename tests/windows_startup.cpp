#include "../src/platform/windows/platform_win.cpp"
#include <httplib.h>
#include <fstream>
#include <iostream>
#include <thread>

using namespace reaweb;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)

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
    const bool docked = argc > 2 && std::string(argv[2]) == "--dock";
    int captures = 0;
    for (int variant = 0; variant < 4; ++variant) for (int run = 0; run < count; ++run) {
      const auto path = root / (std::to_string(variant) + "-" + std::to_string(run)); fs::create_directories(path);
      const auto entry = path / "index.html";
      const auto background = variant == 1 ? "#e8d8c8" : variant == 2 ? "linear-gradient(120deg,#182838,#486878)" : "#182838";
      std::ofstream(entry) << "<!doctype html><style>html{min-height:100%;background:" << background
        << "}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>";
      auto platform = make_platform(path / "profile");
      std::string error;
      WindowOptions options;
      options.entry = entry; options.title = "Startup acceptance"; options.parent = host;
      if (variant == 3) options.url = "http://127.0.0.1:" + std::to_string(port) + "/";
      options.on_message = [](std::string) {};
      options.on_error = [&](std::string message) { std::cerr << message << '\n'; error = std::move(message); };
      auto window = platform->open(std::move(options));
      const auto hwnd = static_cast<HWND>(window->native_handle());
      if (docked) { window->prepare_dock(); SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE); SetParent(hwnd, host); }
      SetWindowPos(hwnd, HWND_TOPMOST, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
      CHECK(!window->visible()); window->show_after_create(); CHECK(!window->visible());
      DWORD cloaked = 0;
      if (!docked) CHECK(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked);
      else {
        auto region = CreateRectRgn(0,0,0,0); const auto type = GetWindowRgn(hwnd,region); DeleteObject(region);
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
      std::cout << "variant=" << variant << " run=" << run << " samples=" << samples << '\n';
    }
    std::cout << "PASS: native first-visible frames, cold profiles=" << count*4 << " captures=" << captures << '\n';
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
