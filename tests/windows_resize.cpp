#include "../src/platform/windows/platform_win.cpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace reaweb;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)

void pump(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  do {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (done()) return;
    Sleep(5);
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("Resize test timed out");
}

struct Surface {
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap;
  HGDIOBJ previous;
  Surface() {
    auto screen = GetDC(nullptr);
    bitmap = CreateCompatibleBitmap(screen, 1000, 800);
    ReleaseDC(nullptr, screen);
    previous = SelectObject(dc, bitmap);
  }
  ~Surface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
};

void dock_redraw_lifecycle() {
  auto root = CreateWindowW(L"STATIC", L"Redraw host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
  auto docker = CreateWindowW(L"STATIC", L"Docker", WS_CHILD | WS_VISIBLE,
    0, 0, 300, 200, root, nullptr, nullptr, nullptr);
  auto first = CreateWindowW(L"STATIC", L"First page", WS_CHILD | WS_VISIBLE,
    0, 0, 100, 100, docker, nullptr, nullptr, nullptr);
  auto second = CreateWindowW(L"STATIC", L"Second page", WS_CHILD | WS_VISIBLE,
    100, 0, 100, 100, docker, nullptr, nullptr, nullptr);
  CHECK(root && docker && first && second);
  WinDockRedraw a, b;
  a.sync(first); b.sync(second);
  for (auto parent : {root, docker}) {
    SendMessageW(parent, WM_SETREDRAW, FALSE, 0);
    CHECK(IsWindowVisible(first) && IsWindowVisible(second));
    SendMessageW(parent, WM_SETREDRAW, TRUE, 0);
  }
  a.detach();
  SendMessageW(root, WM_SETREDRAW, FALSE, 0);
  CHECK(IsWindowVisible(second));
  SendMessageW(root, WM_SETREDRAW, TRUE, 0);
  ShowWindow(second, SW_HIDE);
  SendMessageW(root, WM_SETREDRAW, FALSE, 0);
  CHECK(!IsWindowVisible(root));
  SendMessageW(root, WM_SETREDRAW, TRUE, 0);
  ShowWindow(second, SW_SHOW);
  SendMessageW(root, WM_SETREDRAW, FALSE, 0);
  ShowWindow(root, SW_HIDE);
  SendMessageW(root, WM_SETREDRAW, TRUE, 0);
  CHECK(!IsWindowVisible(root)); // A matching enable must not undo an explicit hide.
  ShowWindow(root, SW_SHOW);
  b.detach();
  SendMessageW(root, WM_SETREDRAW, FALSE, 0);
  CHECK(!IsWindowVisible(root));
  SendMessageW(root, WM_SETREDRAW, TRUE, 0);
  a.sync(first); b.sync(second);
  DestroyWindow(root); // Native destruction removes shared hooks before guard teardown.
}

int main() {
  try {
    dock_redraw_lifecycle();
    unsigned color = 0;
    CHECK(!page_background_message("ordinary message", [&](unsigned c) { color = c; }));
    for (auto text : {"", "-1", "16777216", "99999999999999999", "12suffix"}) {
      CHECK(page_background_message(std::string("__reawebBackground:") + text, [&](unsigned c) { color = c; }));
      CHECK(color == 0);
    }
    const auto root = fs::current_path() / ".cache" / ("resize-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    const auto entry = root / "index.html";
    std::ofstream(entry) << "<!doctype html><style>body{margin:0;background:#182838}"
      "body.light{background:#e8d8c8}</style><input id='state' value='preserved'>";
    std::string error, reply;
    WindowOptions options;
    options.entry = entry; options.title = "WebView resize regression";
    options.script = "addEventListener('DOMContentLoaded',()=>chrome.webview.postMessage('ready'));";
    options.on_message = [&](std::string text) { CHECK(text.rfind("__reawebBackground:", 0) != 0); reply = text; };
    options.on_error = [&](std::string text) { error = text; };
    auto platform = make_platform(root / "profile");
    auto window = platform->open(options);
    auto hwnd = static_cast<HWND>(window->native_handle());
    pump([&] { return reply == "ready" || !error.empty(); });
    CHECK(error.empty());
    const auto evaluate = [&](const std::string& script) {
      reply.clear();
      window->evaluate(script + ";requestAnimationFrame(()=>requestAnimationFrame(()=>chrome.webview.postMessage('done'))); ");
      pump([&] { return reply == "done" || !error.empty(); }); CHECK(error.empty());
    };
    const auto background = [&](COLORREF expected) {
      // Hiding the host exposes the backing paint without any browser-child clipping.
      ShowWindow(hwnd, SW_HIDE);
      Surface surface;
      CHECK(SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(surface.dc), 0) == 1);
      CHECK(GetPixel(surface.dc, 1, 1) == expected);
      ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    };
    evaluate("0"); background(RGB(24, 40, 56));
    evaluate("document.body.className='light'"); background(RGB(232, 216, 200));
    evaluate("document.body.style.background='rgba(0,0,0,.5)'"); background(RGB(255, 255, 255));
    evaluate("document.body.style.background='transparent'"); background(RGB(255, 255, 255));
    evaluate("document.documentElement.style.background='rgb(12,34,56)'"); background(RGB(12, 34, 56));
    evaluate("document.documentElement.style.background='';document.body.style.background='';document.body.className=''");
    background(RGB(24, 40, 56));

    auto docker = CreateWindowExW(0, L"STATIC", L"Docker resize fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(docker);
    for (int cycle = 0; cycle < 3; ++cycle) {
      window->prepare_dock();
      SetParent(hwnd, docker);
      SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE); // REAPER may replace the style.
      CHECK(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CLIPCHILDREN);
      for (int step = 0; step < 36; ++step) {
        const int width = 300 + (step % 9) * 63, height = 100 + (step % 7) * 57;
        SendMessageW(docker, WM_SETREDRAW, FALSE, 0);
        CHECK(IsWindowVisible(hwnd));
        SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SendMessageW(docker, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
      }
      evaluate("if(document.querySelector('#state').value!=='preserved')throw Error('lost state')");
      RECT bounds{}; GetClientRect(hwnd, &bounds);
      reply.clear();
      window->evaluate("chrome.webview.postMessage(JSON.stringify([innerWidth,innerHeight]))");
      pump([&] { return !reply.empty(); });
      CHECK(Json::parse(reply) == Json::array({bounds.right, bounds.bottom}));
      // An explicit erase must leave all browser child pixels untouched.
      Surface surface;
      RECT client{0, 0, bounds.right, bounds.bottom};
      auto brush = CreateSolidBrush(RGB(255, 0, 255));
      FillRect(surface.dc, &client, brush); DeleteObject(brush);
      SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(surface.dc), 0);
      CHECK(GetPixel(surface.dc, 20, 20) == RGB(255, 0, 255));
      window->restore_floating();
      SendMessageW(docker, WM_SETREDRAW, FALSE, 0);
      CHECK(!IsWindowVisible(docker));
      SendMessageW(docker, WM_SETREDRAW, TRUE, 0);
      CHECK(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CLIPCHILDREN);
      for (int step = 0; step < 20; ++step)
        SetWindowPos(hwnd, nullptr, 0, 0, 450 + step * 11, 300 + step * 9, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    window.reset(); platform.reset(); DestroyWindow(docker);
    std::cout << "PASS: native background, theme/transparency, child paint preservation, host redraw suppression/restoration, shared/hidden Docker hosts, dock/float resizing, viewport and page state\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
