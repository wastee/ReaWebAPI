#include "plugin/preferences.hpp"
#include "platform/shared/color_profile.hpp"
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
reaper_plugin_info_t* host;
prefs_page_register_t* page;
std::filesystem::path resource;
void check(bool value) { if (!value) throw std::runtime_error("Preference control mismatch"); }
void run() {
  std::string result;
  HWND dialog = nullptr;
  try {
    dialog = page->create(host->hwnd_main);
    check(dialog && page->par_id == 0x9a && std::string(page->displayname) == "ReaWebAPI");
    const auto checkbox = GetDlgItem(dialog, 1001);
    check(checkbox && !IsWindowEnabled(checkbox));
    check(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED && !GetDlgItem(dialog, 1002));
    for (const auto width : {900, 640, 1100}) {
      SetWindowPos(dialog, nullptr, 0, 0, width, 520, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      RECT client{}, frame{}, button{}, control{};
      GetClientRect(dialog, &client);
      GetWindowRect(GetDlgItem(dialog, 1000), &frame);
      GetWindowRect(GetDlgItem(dialog, 1003), &button);
      GetWindowRect(checkbox, &control);
      POINT origin{0, 0}; ClientToScreen(dialog, &origin);
      check(frame.left == origin.x && frame.top == origin.y &&
        frame.right - frame.left == client.right && frame.bottom - frame.top == client.bottom);
      check(origin.x + client.right - button.right == control.left - origin.x &&
        origin.y + client.bottom - button.bottom == control.left - origin.x);
    }
    CheckDlgButton(dialog, 1001, BST_CHECKED);
    SendMessage(dialog, WM_COMMAND, MAKEWPARAM(1003, BN_CLICKED), 0);
    check(IsDlgButtonChecked(dialog, 1001) == BST_UNCHECKED);
    SendMessage(dialog, WM_USER * 2, 0, 0);
    check(reaweb::startup_color_profile == reaweb::ColorProfile::Default);
    check(!std::filesystem::exists(resource / "ReaWebAPI.ini"));
    result = "PASS: native SWELL Preferences, disabled checkbox, resizing, Restore defaults and unchanged Default";
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
