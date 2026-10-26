#include "runtime/diagnostics.hpp"
#include "runtime/external_settings.hpp"
#include <fstream>
#include <iostream>
#include <set>
using namespace reaweb;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
std::vector<Json> records(const fs::path& file) {
  std::ifstream stream(file); std::string line; std::vector<Json> result;
  while (std::getline(stream, line)) result.push_back(Json::parse(line));
  return result;
}
int main() {
  const auto root = fs::temp_directory_path() / ("reaweb-diagnostics-" + external_token().substr(0, 16));
  try {
    fs::create_directories(root);
    const auto ini = root / "ReaWebAPI.ini";
    std::ofstream(ini) << "[ReaWebAPI]\nIdleWebViews=7\n[ExternalClients]\nEnabled=0\nPort=9123\n";
    DiagnosticSettings settings{true, false, "target"};
    write_diagnostic_settings(ini, settings);
    CHECK(read_diagnostic_settings(ini) == settings);
    CHECK(read_external_settings(ini).port == 9123);
    auto external = read_external_settings(ini); external.port = 9124;
    write_external_settings(ini, external);
    CHECK(read_diagnostic_settings(ini) == settings);
    bool rejected = false;
    try { write_diagnostic_settings(ini, {true, true, "../escape"}); } catch (const Error&) { rejected = true; }
    CHECK(rejected && read_diagnostic_settings(ini) == settings);
    Diagnostics log(root);
    const auto dir = log.directory();
    using L = Diagnostics::Level; using S = Diagnostics::Scope;
    log.event(L::Debug, S::Runtime, "target.runtime", "target", 1);
    log.event(L::Debug, S::App, "target.app", "target", 2);
    log.event(L::Debug, S::Runtime, "other.runtime", "other", 3);
    log.event(L::Debug, S::App, "other.app", "other", 3);
    CHECK(records(dir / "runtime.log").size() == 1);
    CHECK(records(dir / "app-target.log").size() == 2);
    CHECK(!fs::exists(dir / "app-other.log"));
    log.configure({false, true, ""});
    log.event(L::Debug, S::Runtime, "other.runtime", "other", 3);
    log.event(L::Debug, S::App, "other.app", "other", 3);
    CHECK(records(dir / "app-other.log").size() == 1);
    log.configure({});
    log.event(L::Info, S::Runtime, "off");
    log.event(L::Warn, S::App, "warning", "other", 3, "TIMEOUT", {{"message", "secret"}, {"token", "secret"}, {"generation", 2}});
    const auto warnings = records(dir / "app-other.log");
    CHECK(warnings.size() == 2 && warnings.back()["level"] == "WARN");
    CHECK(!warnings.back().contains("message") && !warnings.back().contains("token"));
    CHECK(warnings.back()["generation"] == 2 && warnings.back()["sessionId"] == warnings.front()["sessionId"]);
    const auto bytes = fs::file_size(dir / "runtime.log");
    std::thread realtime([&] { log.event(L::Error, S::Runtime, "must.not.write"); log.configure({true, true, "other"}); });
    realtime.join();
    CHECK(fs::file_size(dir / "runtime.log") == bytes && !log.enabled(L::Debug, S::Runtime));
    for (int i = 0; i < 100; ++i) log.event(L::Error, S::App, "repeat", "rate", 1);
    CHECK(records(dir / "app-rate.log").size() == 20);
    for (int i = 0; i < 18000; ++i) log.event(L::Error, S::App, "rotation", "rotate", i);
    CHECK(fs::exists(dir / "app-rotate.log.1"));
    for (int i = 0; i < 80; ++i) log.event(L::Warn, S::App, "bounded", "retention-" + std::to_string(i));
    size_t count = 0;
    for (const auto& file : fs::directory_iterator(dir)) { ++count; CHECK(file.file_size() <= Diagnostics::file_limit); }
    CHECK(count <= Diagnostics::file_count);
    CHECK(Diagnostics::filename("con") == "app-con.log");
    rejected = false;
    try { Diagnostics::filename("../escape"); } catch (...) { rejected = true; }
    CHECK(rejected);
    std::ofstream(root / "unwritable") << "file";
    Diagnostics unavailable(root / "unwritable");
    unavailable.event(L::Error, S::Runtime, "must.not.throw");
    fs::remove_all(root);
    std::cout << "Diagnostics: filtering, persistence, privacy, thread rejection, rotation, retention and I/O isolation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; std::error_code ignored; fs::remove_all(root, ignored); return 1;
  }
}
