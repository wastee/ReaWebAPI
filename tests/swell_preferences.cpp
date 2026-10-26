#include "plugin/preferences.hpp"
#include "platform/shared/color_profile.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace {
reaper_plugin_info_t* host;
prefs_page_register_t* page;
std::filesystem::path resource;
void check_impl(bool value, int line) { if (!value) throw std::runtime_error("Preference control mismatch at " + std::to_string(line)); }
#define check(value) check_impl(value, __LINE__)
void run() {
  std::string result;
  HWND dialog = nullptr;
  try {
    const bool idle_test = std::filesystem::exists(resource / "preferences-test.idle");
    dialog = page->create(host->hwnd_main);
    check(dialog && page->par_id == 0x9a && std::string(page->displayname) == "ReaWebAPI");
    const auto checkbox = GetDlgItem(dialog, 1001);
    check(checkbox && !IsWindowEnabled(checkbox));
    check(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED && !GetDlgItem(dialog, 1002));
    check(GetDlgItemInt(dialog, 1004, nullptr, FALSE) == 2);
    if (!idle_test) for (const auto width : {900, 640, 1100}) {
      SetWindowPos(dialog, nullptr, 0, 0, width, 520, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      SendMessage(dialog, WM_SIZE, 0, 0);
      RECT client{}, frame{}, button{}, control{};
      GetClientRect(dialog, &client);
      GetWindowRect(GetDlgItem(dialog, 1000), &frame);
      GetWindowRect(GetDlgItem(dialog, 1003), &button);
      GetWindowRect(checkbox, &control);
      POINT origin{0, 0}; ClientToScreen(dialog, &origin);
      check(frame.left == origin.x && frame.top == origin.y &&
        frame.right - frame.left == client.right && frame.bottom - frame.top == client.bottom);
      const auto inset = control.left - origin.x;
      const auto right = origin.x + client.right - button.right, bottom = origin.y + client.bottom - std::max(button.top, button.bottom);
      if (right != inset || bottom != inset) throw std::runtime_error("Layout inset=" + std::to_string(inset) + ", right=" + std::to_string(right) + ", bottom=" + std::to_string(bottom));
    }
    if (std::filesystem::exists(resource / "preferences-test.diagnostics")) {
      DestroyWindow(dialog);
      reaweb::DiagnosticSettings applied;
      int opened = 0;
      page = reaweb::initialize_preferences(nullptr, resource, {}, {}, [&](const auto& settings) { applied = settings; }, [&] { ++opened; });
      dialog = page->create(host->hwnd_main);
      check(IsDlgButtonChecked(dialog, 1021) == BST_UNCHECKED && IsDlgButtonChecked(dialog, 1023) == BST_UNCHECKED);
      SetDlgItemText(dialog, 1022, "target-app"); CheckDlgButton(dialog, 1021, BST_CHECKED);
      DestroyWindow(dialog); dialog = page->create(host->hwnd_main);
      check(IsDlgButtonChecked(dialog, 1021) == BST_UNCHECKED);
      SetDlgItemText(dialog, 1022, "target-app"); CheckDlgButton(dialog, 1021, BST_CHECKED);
      check(SendMessage(dialog, WM_USER * 2, 0, 0) != 0);
      check(applied.app_debug && !applied.runtime_debug && applied.target == "target-app");
      CheckDlgButton(dialog, 1023, BST_CHECKED); check(SendMessage(dialog, WM_USER * 2, 0, 0) != 0);
      check(applied.runtime_debug && reaweb::read_diagnostic_settings(resource / "ReaWebAPI.ini") == applied);
      SendMessage(dialog, WM_COMMAND, MAKEWPARAM(1024, BN_CLICKED), 0); check(opened == 1);
      result = "PASS: Diagnostics native controls, independent toggles, target, Cancel, Apply, persistence and folder callback";
    } else if (idle_test) {
      SetDlgItemInt(dialog, 1004, 7, FALSE); DestroyWindow(dialog);
      unsigned applied = 99;
      page = reaweb::initialize_preferences(nullptr, resource, {}, [&](unsigned count) { applied = count; });
      dialog = page->create(host->hwnd_main);
      check(GetDlgItemInt(dialog, 1004, nullptr, FALSE) == 2 && applied == 99);
      for (unsigned count : {7u, 0u, 32u, 2u}) {
        SetDlgItemInt(dialog, 1004, count, FALSE);
        check(SendMessage(dialog, WM_USER * 2, 0, 0) != 0);
        check(applied == count && reaweb::idle_webview_preferences() == count);
        check(GetPrivateProfileInt("ReaWebAPI", "IdleWebViews", 99, (resource / "ReaWebAPI.ini").c_str()) == count);
      }
      DestroyWindow(dialog); dialog = nullptr;
      reaweb::initialize_preferences(nullptr, resource);
      check(reaweb::idle_webview_preferences() == 2);
      result = "PASS: native SWELL idle-container Preferences, default 2, Cancel, Apply 7/0/32/2 and persistence";
    } else {
      CheckDlgButton(dialog, 1001, BST_CHECKED);
      SendMessage(dialog, WM_COMMAND, MAKEWPARAM(1003, BN_CLICKED), 0);
      check(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED);
      SendMessage(dialog, WM_USER * 2, 0, 0);
      check(reaweb::startup_color_profile == reaweb::ColorProfile::Default);
      check(!std::filesystem::exists(resource / "ReaWebAPI.ini"));
      result = "PASS: native SWELL Preferences, disabled checkbox, resizing, Restore defaults and unchanged Default";
    }
  } catch (const std::exception& error) { result = std::string("FAIL: ") + error.what(); }
  if (dialog) DestroyWindow(dialog);
  std::ofstream(resource / "preferences-test.log") << result << '\n';
}
void finish() {
  host->Register("-timer", reinterpret_cast<void*>(finish));
  reinterpret_cast<void(*)(int,int)>(host->GetFunc("Main_OnCommand"))(40004, 0);
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = std::filesystem::u8path(reinterpret_cast<const char*(*)()>(rec->GetFunc("GetResourcePath"))());
  if (!std::filesystem::exists(resource / "preferences-test.enabled")) return 0;
  page = reaweb::initialize_preferences(instance, resource);
  run();
  rec->Register("timer", reinterpret_cast<void*>(finish));
  return 1;
}
