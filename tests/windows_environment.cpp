#include "platform/shared/color_profile.hpp"
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl.h>
#include <vector>
#include <iostream>
namespace {
std::vector<Microsoft::WRL::ComPtr<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>> completions;
bool immediate_failure = false;
HRESULT test_environment(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) {
  completions.emplace_back(callback);
  return immediate_failure ? E_FAIL : S_OK;
}
}
#define CreateCoreWebView2EnvironmentWithOptions test_environment
#include "../src/platform/windows/platform_win.cpp"
#undef CreateCoreWebView2EnvironmentWithOptions
using namespace reaweb;
#define CHECK(v) do { if (!(v)) throw std::runtime_error("Check failed: " #v); } while (false)
int main() {
  try {
    int errors = 0;
    WindowOptions options;
    options.hidden = true; options.url = "http://127.0.0.1/";
    options.on_message = [](std::string) {};
    options.on_error = [&](std::string) { ++errors; };
    auto platform = make_platform(fs::current_path() / ".cache/environment-test");
    CHECK(completions.size() == 1);
    auto first = platform->open(options), second = platform->open(options);
    CHECK(completions.size() == 1);
    first->cancel_pending_show();
    auto closed = platform->open(options);
    SendMessageW(static_cast<HWND>(closed->native_handle()), WM_CLOSE, 0, 0);
    CHECK(closed->closed());
    completions[0]->Invoke(E_FAIL, nullptr);
    CHECK(errors == 1 && second->closed());
    first.reset(); second.reset(); closed.reset();
    auto retry = platform->open(options);
    CHECK(completions.size() == 2);
    completions[0]->Invoke(E_FAIL, nullptr);
    CHECK(errors == 1 && !retry->closed());
    completions[1]->Invoke(E_FAIL, nullptr);
    CHECK(errors == 2 && retry->closed());
    retry.reset();
    auto pending = platform->open(options);
    CHECK(completions.size() == 3);
    pending.reset(); platform.reset();
    completions[2]->Invoke(E_FAIL, nullptr);
    CHECK(errors == 2);
    immediate_failure = true;
    platform = make_platform(fs::current_path() / ".cache/environment-test");
    CHECK(completions.size() == 4);
    bool failed = false;
    try { platform->open(options); } catch (const std::exception&) { failed = true; }
    CHECK(failed && completions.size() == 5);
    immediate_failure = false;
    pending = platform->open(options);
    CHECK(completions.size() == 6);
    completions[4]->Invoke(E_FAIL, nullptr);
    CHECK(!pending->closed());
    completions[5]->Invoke(E_FAIL, nullptr);
    CHECK(pending->closed() && errors == 3);
    std::cout << "PASS: shared initialization, close, retry, late callbacks and teardown\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
