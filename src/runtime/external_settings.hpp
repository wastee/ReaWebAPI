#pragma once
#include "core/core.hpp"

namespace reaweb {
struct ExternalSettings {
  bool enabled = false;
  uint16_t port = 9123;
  std::string token;
};
std::string external_token();
ExternalSettings read_external_settings(const fs::path& file);
void write_external_settings(const fs::path& file, const ExternalSettings& settings);
}
