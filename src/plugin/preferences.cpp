#include "plugin/preferences.hpp"
#include "platform/shared/color_profile.hpp"
#include <algorithm>
#include <string>
#include <cstring>

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
ExternalSettings saved_external;
std::string external_read_error;
std::function<void(const ExternalSettings&)> external_apply;
constexpr int frame_control = 1000, profile_control = 1001, restore_control = 1003;
constexpr int enable_control = 1010, port_control = 1011, token_control = 1012, copy_control = 1013, regenerate_control = 1014;

void set_text(HWND window, int control, const std::string& text) {
#ifdef _WIN32
  SetDlgItemTextA(window, control, text.c_str());
#else
  SetDlgItemText(window, control, text.c_str());
#endif
}
void error_message(HWND window, const char* message) {
#ifdef _WIN32
  MessageBoxA(window, message, "ReaWebAPI settings", MB_OK | MB_ICONERROR);
#else
  MessageBox(window, message, "ReaWebAPI settings", MB_OK | MB_ICONERROR);
#endif
}
void show_token(HWND window) {
  set_text(window, token_control, saved_external.token);
  EnableWindow(GetDlgItem(window, copy_control), !saved_external.token.empty());
}
void save_external(const ExternalSettings& settings) {
  if (settings.enabled == saved_external.enabled && settings.port == saved_external.port && settings.token == saved_external.token) {
    if (external_apply) external_apply(settings);
    return;
  }
  write_external_settings(settings_file, settings);
  try { if (external_apply) external_apply(settings); }
  catch (...) { write_external_settings(settings_file, saved_external); throw; }
  saved_external = settings;
}

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
    CheckDlgButton(window, enable_control, saved_external.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(window, port_control, saved_external.port, FALSE);
    show_token(window);
    if (!external_read_error.empty()) error_message(window, external_read_error.c_str());
    resize(window);
    return FALSE;
  }
  if (message == WM_SIZE) { resize(window); return TRUE; }
  if (message == WM_COMMAND && LOWORD(command) == copy_control && HIWORD(command) == BN_CLICKED) {
    if (!saved_external.token.empty() && OpenClipboard(window)) {
      auto memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<int>(saved_external.token.size() + 1));
      if (memory) {
        auto pointer = GlobalLock(memory);
        if (pointer) {
          std::memcpy(pointer, saved_external.token.c_str(), saved_external.token.size() + 1);
          GlobalUnlock(memory); EmptyClipboard(); SetClipboardData(CF_TEXT, memory);
        } else GlobalFree(memory);
      }
      CloseClipboard();
    }
    return TRUE;
  }
  if (message == WM_COMMAND && LOWORD(command) == regenerate_control && HIWORD(command) == BN_CLICKED) {
    try { auto settings = saved_external; settings.token = external_token(); save_external(settings); show_token(window); }
    catch (const std::exception& error) { error_message(window, error.what()); }
    return TRUE;
  }
  if (message == WM_COMMAND && ((LOWORD(command) == enable_control && HIWORD(command) == BN_CLICKED) ||
      (LOWORD(command) == port_control && HIWORD(command) == EN_CHANGE))) {
    EnableWindow(GetDlgItem(GetParent(window), 0x478), TRUE);
    return TRUE;
  }
  if (message == WM_COMMAND && HIWORD(command) == BN_CLICKED &&
      (LOWORD(command) == profile_control || LOWORD(command) == restore_control)) {
    if (LOWORD(command) == restore_control) {
      if (MessageBox(window, TEXT("Are you sure you want to restore the default settings?"),
          TEXT("ReaWebAPI settings"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return TRUE;
      CheckDlgButton(window, profile_control, BST_UNCHECKED);
      CheckDlgButton(window, enable_control, BST_UNCHECKED);
      SetDlgItemInt(window, port_control, 9123, FALSE);
    }
    EnableWindow(GetDlgItem(GetParent(window), 0x478), TRUE); // REAPER Preferences Apply
    return TRUE;
  }
  if (message == WM_USER * 2) {
    try {
      auto settings = saved_external;
      settings.enabled = IsDlgButtonChecked(window, enable_control) == BST_CHECKED;
      BOOL translated = FALSE;
      const auto port = GetDlgItemInt(window, port_control, &translated, FALSE);
      if (!translated || port < 1 || port > 65535) throw Error("INVALID_ARGUMENT", "Port must be an integer from 1 to 65535.");
      settings.port = static_cast<uint16_t>(port);
      if (settings.enabled && settings.token.empty()) settings.token = external_token();
      save_external(settings); show_token(window);
    } catch (const std::exception& error) { error_message(window, error.what()); return FALSE; }
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

const ExternalSettings& external_preferences() { return saved_external; }
prefs_page_register_t* initialize_preferences(REAPER_PLUGIN_HINSTANCE instance, const std::filesystem::path& resource,
    std::function<void(const ExternalSettings&)> apply_external) {
  module = instance;
  settings_file = resource / "ReaWebAPI.ini";
  external_apply = std::move(apply_external);
  external_read_error.clear(); saved_external = {};
  try {
    saved_external = read_external_settings(settings_file);
    if (saved_external.enabled && saved_external.token.empty()) {
      saved_external.token = external_token(); write_external_settings(settings_file, saved_external);
    }
  } catch (const std::exception& error) { saved_external = {}; external_read_error = error.what(); }
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
