#pragma once

namespace reaweb {
enum class ColorProfile { Default, SRGB };

// Loaded once at extension startup. WebView2 environments sharing browser data
// must use identical arguments, including after the last window is closed.
inline ColorProfile startup_color_profile = ColorProfile::Default;

inline constexpr bool supports_srgb_profile() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}
}
