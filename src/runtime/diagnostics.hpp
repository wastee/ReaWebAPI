#pragma once
#include "core/core.hpp"
#include <filesystem>
#include <thread>
#include <map>
#include <chrono>

namespace reaweb {
struct DiagnosticSettings {
  bool app_debug = false, runtime_debug = false;
  std::string target;
  bool operator==(const DiagnosticSettings& other) const {
    return app_debug == other.app_debug && runtime_debug == other.runtime_debug && target == other.target;
  }
};
DiagnosticSettings read_diagnostic_settings(const std::filesystem::path& file) noexcept;
void write_diagnostic_settings(const std::filesystem::path& file, const DiagnosticSettings& settings);

// Called only from the owning UI thread. Other threads are rejected before any I/O or lock.
class Diagnostics {
public:
  enum class Level { Error, Warn, Info, Debug };
  enum class Scope { Runtime, App };
  explicit Diagnostics(const std::filesystem::path& resource) noexcept;
  void configure(const DiagnosticSettings& settings) noexcept;
  bool enabled(Level level, Scope scope, const std::string& app = {}) const noexcept;
  void event(Level level, Scope scope, const char* event, const std::string& app = {}, int window = 0,
    const char* result = "ok", const Json& fields = Json()) noexcept;
  const std::filesystem::path& directory() const { return directory_; }
  static std::string filename(const std::string& app);
  static constexpr uintmax_t file_limit = 2 * 1024 * 1024;
  static constexpr size_t file_count = 64;
private:
  std::thread::id owner_ = std::this_thread::get_id();
  DiagnosticSettings settings_;
  std::filesystem::path directory_;
  std::string session_;
  using Clock = std::chrono::steady_clock;
  struct Rate { Clock::time_point start{}; unsigned count = 0, suppressed = 0; };
  std::map<std::string, Rate> rates_;
  void write(const std::filesystem::path& path, const std::string& line);
};
}
