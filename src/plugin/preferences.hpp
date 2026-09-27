#pragma once
#include <reaper_plugin.h>
#include <filesystem>

namespace reaweb {
prefs_page_register_t* initialize_preferences(REAPER_PLUGIN_HINSTANCE instance,
  const std::filesystem::path& resource);
}
