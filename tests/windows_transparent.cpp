// Opt-in desktop pixel acceptance with a changing native background.
#include "../src/platform/windows/platform_win.cpp"
#include <fstream>
#include <iostream>
using namespace reaweb;
#define CHECK(v) do { if (!(v)) throw std::runtime_error(#v); } while (false)
static COLORREF background = RGB(20, 80, 180);
static LRESULT CALLBACK background_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{}; auto dc = BeginPaint(hwnd, &paint);
    auto brush = CreateSolidBrush(background); FillRect(dc, &paint.rcPaint, brush); DeleteObject(brush);
    EndPaint(hwnd, &paint); return 0;
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}
int main(int argc, char**) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    startup_color_profile = argc > 1 ? ColorProfile::SRGB : ColorProfile::Default;
    const auto path = fs::current_path() / ".cache" / ("alpha-win-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(path);
    std::ofstream(path / "index.html") << R"(<!doctype html><style>html,body{background:transparent;margin:0}div{position:absolute;left:100px;top:100px;width:200px;height:160px;background:rgba(255,0,0,.5);border-radius:20px;box-shadow:0 8px 15px #0008}</style><div></div>)";
    auto platform = make_platform(path / "profile");
    WNDCLASSW cls{}; cls.lpfnWndProc = background_proc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"AlphaBackground";
    RegisterClassW(&cls);
    auto host = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, L"Alpha background", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      50, 50, 1000, 750, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    struct Cleanup { HWND value; ~Cleanup() { DestroyWindow(value); } } cleanup{host};
    for (bool framed : {false, true}) for (bool hidden : {false, true}) {
      std::string error;
      WindowOptions options; options.entry = path / "index.html"; options.title = "Alpha acceptance";
      options.parent = host; options.transparent = true; options.frameless = !framed; options.hidden = hidden;
      options.on_message = [](std::string) {}; options.on_error = [&](std::string value) { error = value; };
      auto window = platform->open(std::move(options));
      window->restore_placement({{"x", 150}, {"y", 150}, {"width", 600}, {"height", 420}});
      window->show_after_create();
      auto hwnd = static_cast<HWND>(window->native_handle());
      const auto pump = [&](unsigned duration) {
        const auto start = GetTickCount64();
        do {
          MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
          platform->pump(); window->tick(); CHECK(error.empty()); Sleep(2);
        } while (GetTickCount64() - start < duration);
      };
      pump(2000); CHECK(window->transparent_active());
      if (hidden) { CHECK(!window->visible()); window->show(); }
      window->focus(); pump(300);
      POINT origin{}; ClientToScreen(hwnd, &origin);
      const auto hit = WindowFromPoint({origin.x + 40, origin.y + 40});
      CHECK(hit == hwnd || IsChild(hwnd, hit));
      COLORREF previous_half = 0, previous_color = 0;
      for (const auto color : {RGB(20, 80, 180), RGB(80, 160, 40)}) {
        background = color;
        RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        pump(250); DwmFlush();
        auto screen = GetDC(nullptr);
        const auto clear = GetPixel(screen, origin.x + 40, origin.y + 40);
        const auto half = GetPixel(screen, origin.x + 160, origin.y + 160);
        ReleaseDC(nullptr, screen);
        std::cout << "framed=" << framed << " hidden=" << hidden << " clear=" << clear << " expected=" << color << " half=" << half << '\n';
        CHECK(clear == color);
        if (previous_half) {
          // Color management can transform CSS red. Changing only the native
          // background must still contribute exactly half of the pixel delta.
          for (int shift : {0, 8, 16}) {
            const int delta = int((half >> shift) & 255) - int((previous_half >> shift) & 255);
            const int expected = (int((color >> shift) & 255) - int((previous_color >> shift) & 255)) / 2;
            CHECK(std::abs(delta - expected) <= 3);
          }
        }
        previous_half = half; previous_color = color;
      }
      window->set_visible(false); CHECK(window->transparent_active()); window->show(); pump(200);
      window->reload(); pump(500); CHECK(window->transparent_active());
      CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_LAYERED)));
    }
    std::cout << "PASS: desktop Alpha, half Alpha, framed/frameless, hidden, Hide/Show and Reload\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
