#pragma once
#include <reaper_plugin.h>
#include <filesystem>
#include "runtime/external_settings.hpp"

namespace reaweb {
prefs_page_register_t* initialize_preferences(REAPER_PLUGIN_HINSTANCE instance,
  const std::filesystem::path& resource, std::function<void(const ExternalSettings&)> apply_external = {},
  std::function<void(unsigned)> apply_webviews = {});
const ExternalSettings& external_preferences();
unsigned idle_webview_preferences();
}
