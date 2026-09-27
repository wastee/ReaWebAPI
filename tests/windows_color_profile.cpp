#include "platform/shared/color_profile.hpp"
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <fstream>
#include <iostream>
#include <chrono>

namespace {
std::wstring browser_arguments;
HRESULT inspect_environment(PCWSTR browser, PCWSTR data, ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) {
  LPWSTR arguments = nullptr;
  if (options) options->get_AdditionalBrowserArguments(&arguments);
  browser_arguments = arguments ? arguments : L"";
  CoTaskMemFree(arguments);
  return CreateCoreWebView2EnvironmentWithOptions(browser, data, options, callback);
}
}
#define CreateCoreWebView2EnvironmentWithOptions inspect_environment
#include "../src/platform/windows/platform_win.cpp"
#undef CreateCoreWebView2EnvironmentWithOptions
#include "plugin/preferences.hpp"

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
  throw std::runtime_error("WebView2 startup timed out");
}

void check_layout(HWND dialog) {
  RECT client{}, frame{}, button{}, checkbox{};
  GetClientRect(dialog, &client);
  GetWindowRect(GetDlgItem(dialog, 1000), &frame);
  GetWindowRect(GetDlgItem(dialog, 1003), &button);
  GetWindowRect(GetDlgItem(dialog, 1001), &checkbox);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&frame), 2);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&button), 2);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&checkbox), 2);
  CHECK(frame.left == 0 && frame.top == 0 && frame.right == client.right && frame.bottom == client.bottom);
  CHECK(client.right - button.right == checkbox.left && client.bottom - button.bottom == checkbox.left);
  int controls = 0;
  for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ++controls;
  CHECK(controls == 3);
}

int main(int argc, char**) {
  try {
    const bool srgb = argc > 1;
    const auto root = fs::current_path() / ("color-profile-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    const auto ini = root / "ReaWebAPI.ini";
    const auto module = GetModuleHandleW(nullptr);
    auto page = initialize_preferences(module, root);
    CHECK(std::string(page->displayname) == "ReaWebAPI" && page->par_id == 0x9a);
    CHECK(startup_color_profile == ColorProfile::Default);
    auto parent = CreateWindowExW(0, L"STATIC", L"Preferences", WS_OVERLAPPEDWINDOW,
      0, 0, 720, 500, nullptr, nullptr, module, nullptr);
    CHECK(parent);
    auto apply = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD,
      0, 0, 50, 24, parent, reinterpret_cast<HMENU>(0x478), module, nullptr);
    EnableWindow(apply, FALSE);
    auto dialog = page->create(parent);
    CHECK(dialog && IsWindowEnabled(GetDlgItem(dialog, 1001)));
    check_layout(dialog);
    for (const auto width : {1000, 640, 1200}) {
      SetWindowPos(dialog, nullptr, 0, 0, width, 520, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      check_layout(dialog);
    }
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED);
    SendDlgItemMessageW(dialog, 1001, BM_CLICK, 0, 0);
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_CHECKED);
    CHECK(IsWindowEnabled(apply) && !fs::exists(ini));
    DestroyWindow(dialog); // Cancel discards the selection.
    dialog = page->create(parent);
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED);
    SendDlgItemMessageW(dialog, 1001, BM_CLICK, 0, 0);
    SendMessageW(dialog, WM_USER * 2, 0, 0);
    CHECK(fs::exists(ini) && startup_color_profile == ColorProfile::Default);
    DestroyWindow(dialog);
    page = initialize_preferences(module, root); // Simulate the next REAPER startup.
    CHECK(startup_color_profile == ColorProfile::SRGB);
    dialog = page->create(parent);
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_CHECKED);
    EnableWindow(apply, FALSE);
    SendDlgItemMessageW(dialog, 1003, BM_CLICK, 0, 0);
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED && IsWindowEnabled(apply));
    DestroyWindow(dialog);
    dialog = page->create(parent);
    CHECK(IsDlgButtonChecked(dialog, 1001) == BST_CHECKED); // Cancel also discards Restore defaults.
    SendDlgItemMessageW(dialog, 1003, BM_CLICK, 0, 0);
    SendMessageW(dialog, WM_USER * 2, 0, 0);
    CHECK(startup_color_profile == ColorProfile::SRGB);
    DestroyWindow(dialog);
    initialize_preferences(module, root);
    CHECK(startup_color_profile == ColorProfile::Default);
    CHECK(WritePrivateProfileStringW(L"ReaWebAPI", L"WebViewColorProfile", L"invalid", ini.c_str()));
    initialize_preferences(module, root);
    CHECK(startup_color_profile == ColorProfile::Default);
    CHECK(WritePrivateProfileStringW(L"ReaWebAPI", L"WebViewColorProfile", srgb ? L"sRGB" : L"Default", ini.c_str()));
    initialize_preferences(module, root);
    DestroyWindow(parent);

    const auto entry = root / "index.html";
    std::ofstream(entry) << "<!doctype html><title>Color profile</title><body style='background:#808080'>sRGB</body>";
    auto platform = make_platform(root / "WebViewData");
    CHECK(browser_arguments == (srgb ? L"--force-color-profile=srgb" : L""));
    std::string error;
    int ready = 0;
    WindowOptions options;
    options.entry = entry; options.title = "Color profile regression";
    options.script = "addEventListener('DOMContentLoaded',()=>chrome.webview.postMessage('ready'));";
    options.on_message = [&](std::string value) { if (value == "ready") ++ready; };
    options.on_error = [&](std::string value) { error = value; };
    auto first = platform->open(options), second = platform->open(options);
    pump([&] { return ready == 2 || !error.empty(); });
    CHECK(error.empty() && ready == 2);
    first.reset(); second.reset();
    auto reopened = platform->open(options);
    pump([&] { return ready == 3 || !error.empty(); });
    CHECK(error.empty() && ready == 3);
    reopened.reset(); platform.reset();
    std::cout << "PASS: Preferences checkbox, resizing, Restore defaults, Cancel/Apply, persistence, restart boundary, invalid value, "
      << (srgb ? "sRGB" : "Default") << " environment and multiple/reopened WebViews\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
