#include "plugin/preferences.hpp"
#include "platform/shared/color_profile.hpp"
#include <algorithm>
#include <string>

#ifndef _WIN32
#define TEXT(value) value
#include <swell/swell-dlggen.h>
SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(201, SWELL_DLG_WS_CHILD, "ReaWebAPI settings", 330, 220)
#include "preferences_controls.inc"
SWELL_DEFINE_DIALOG_RESOURCE_END2(201)
#endif

namespace reaweb {
namespace {
REAPER_PLUGIN_HINSTANCE module;
std::filesystem::path settings_file;
ColorProfile saved_profile = ColorProfile::Default;
constexpr int frame_control = 1000, profile_control = 1001, restore_control = 1003;

void resize(HWND window) {
  const auto frame = GetDlgItem(window, frame_control), restore = GetDlgItem(window, restore_control);
  if (!frame || !restore) return;
  RECT client{}, button{}, checkbox{};
  GetClientRect(window, &client);
  GetWindowRect(restore, &button);
  GetWindowRect(GetDlgItem(window, profile_control), &checkbox);
  POINT inset{checkbox.left, checkbox.top};
  ScreenToClient(window, &inset);
  const int width = button.right - button.left, height = button.bottom - button.top;
  SetWindowPos(frame, nullptr, 0, 0, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(restore, nullptr, std::max(0, static_cast<int>(client.right - inset.x) - width),
    std::max(0, static_cast<int>(client.bottom - inset.x) - height), width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(window, nullptr, TRUE);
}

INT_PTR CALLBACK procedure(HWND window, UINT message, WPARAM command, LPARAM) {
  if (message == WM_INITDIALOG) {
    CheckDlgButton(window, profile_control, saved_profile == ColorProfile::SRGB ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(window, profile_control), supports_srgb_profile());
    resize(window);
    return FALSE;
  }
  if (message == WM_SIZE) { resize(window); return TRUE; }
  if (message == WM_COMMAND && HIWORD(command) == BN_CLICKED &&
      (LOWORD(command) == profile_control || LOWORD(command) == restore_control)) {
    if (LOWORD(command) == restore_control) {
      if (MessageBox(window, TEXT("Are you sure you want to restore the default settings?"),
          TEXT("ReaWebAPI settings"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return TRUE;
      if (IsDlgButtonChecked(window, profile_control) == BST_UNCHECKED) return TRUE;
      CheckDlgButton(window, profile_control, BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(GetParent(window), 0x478), TRUE); // REAPER Preferences Apply
    return TRUE;
  }
  if (message == WM_USER * 2 && supports_srgb_profile()) {
#ifdef _WIN32
    const auto profile = IsDlgButtonChecked(window, profile_control) == BST_CHECKED ? ColorProfile::SRGB : ColorProfile::Default;
    if (profile == saved_profile) return TRUE;
    if (!WritePrivateProfileStringW(L"ReaWebAPI", L"WebViewColorProfile",
        profile == ColorProfile::SRGB ? L"sRGB" : L"Default", settings_file.c_str())) {
      MessageBox(window, TEXT("Could not save the WebView color profile."), TEXT("ReaWebAPI settings"), MB_OK | MB_ICONERROR);
      return FALSE;
    }
    saved_profile = profile;
#endif
    return TRUE;
  }
  return FALSE;
}

HWND create(HWND parent) { return CreateDialogParam(module, MAKEINTRESOURCE(201), parent, procedure, 0); }
prefs_page_register_t page{"reawebapi", "ReaWebAPI", create, 0x9a, "", 0, nullptr, nullptr, {}};
}

prefs_page_register_t* initialize_preferences(REAPER_PLUGIN_HINSTANCE instance, const std::filesystem::path& resource) {
  module = instance;
  settings_file = resource / "ReaWebAPI.ini";
  saved_profile = ColorProfile::Default;
#ifdef _WIN32
  wchar_t value[32]{};
  GetPrivateProfileStringW(L"ReaWebAPI", L"WebViewColorProfile", L"Default", value, 32, settings_file.c_str());
  if (std::wstring(value) == L"sRGB") saved_profile = ColorProfile::SRGB;
#endif
  startup_color_profile = saved_profile;
  return &page;
}
}
