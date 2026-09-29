#include "runtime/external_settings.hpp"
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#include <cerrno>
#endif

namespace reaweb {
std::string external_token() {
  unsigned char bytes[32];
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw Error("TOKEN_UNAVAILABLE", "Cannot generate an access token");
#elif defined(__APPLE__)
  arc4random_buf(bytes, sizeof(bytes));
#else
  for (size_t offset = 0; offset < sizeof(bytes);) {
    auto n = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw Error("TOKEN_UNAVAILABLE", "Cannot generate an access token");
    offset += size_t(n);
  }
#endif
  std::string token;
  for (auto byte : bytes) { token += "0123456789abcdef"[byte >> 4]; token += "0123456789abcdef"[byte & 15]; }
  return token;
}
namespace {
std::string read(const fs::path& file) {
  if (!fs::exists(file)) return {};
  if (fs::file_size(file) > 1024 * 1024) throw Error("SETTINGS_INVALID", "ReaWebAPI.ini exceeds 1 MiB");
  std::ifstream input(file, std::ios::binary);
  if (!input) throw Error("SETTINGS_INVALID", "Cannot read ReaWebAPI.ini");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string trim(std::string text) {
  const auto a = text.find_first_not_of(" \t\r"), b = text.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : text.substr(a, b - a + 1);
}
bool valid_token(const std::string& token) {
  return token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos;
}
}
ExternalSettings read_external_settings(const fs::path& file) {
  ExternalSettings settings;
  std::istringstream input(read(file)); std::string line; bool section = false;
  while (std::getline(input, line)) {
    line = trim(line);
    if (!line.empty() && line[0] == '[') { section = line == "[ExternalClients]"; continue; }
    if (!section) continue;
    const auto equals = line.find('=');
    if (equals == std::string::npos) continue;
    const auto key = trim(line.substr(0, equals)), value = trim(line.substr(equals + 1));
    if (key == "Enabled") settings.enabled = value == "1";
    if (key == "Port") {
      if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos ||
          std::stoul(value) == 0 || std::stoul(value) > 65535) throw Error("SETTINGS_INVALID", "External Client port must be 1..65535");
      settings.port = uint16_t(std::stoul(value));
    }
    if (key == "AccessToken") settings.token = value;
  }
  if (!settings.token.empty() && !valid_token(settings.token)) throw Error("SETTINGS_INVALID", "Invalid External Client token in ReaWebAPI.ini");
  return settings;
}
void write_external_settings(const fs::path& file, const ExternalSettings& settings) {
  if (!settings.port || (!settings.token.empty() && !valid_token(settings.token)) || (settings.enabled && settings.token.empty()))
    throw Error("SETTINGS_INVALID", "Invalid External Client settings");
  std::istringstream input(read(file)); std::string line, output; bool section = false;
  while (std::getline(input, line)) {
    const auto text = trim(line);
    if (!text.empty() && text[0] == '[') section = text == "[ExternalClients]";
    if (!section) output += line + "\n";
  }
  output += "[ExternalClients]\nEnabled=" + std::string(settings.enabled ? "1" : "0") +
    "\nPort=" + std::to_string(settings.port) + "\nAccessToken=" + settings.token + "\n";
  auto temporary = file; temporary += ".external-" + external_token().substr(0, 16) + ".tmp";
  try {
    fs::create_directories(file.parent_path());
    { std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
#ifndef _WIN32
      fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write);
#endif
      stream << output; stream.flush();
      if (!stream) throw Error("SETTINGS_IO", "Cannot save External Client settings"); }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw Error("SETTINGS_IO", "Cannot replace ReaWebAPI.ini");
#else
    fs::rename(temporary, file);
#endif
  } catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
}
}
