#include "core/core.hpp"
#include "core/native.hpp"
#include <algorithm>

namespace reaweb {
ReaperApiCore::ReaperApiCore(Host& host, std::string session, bool strict_handles)
  : host_(host), native_(std::make_unique<NativeContext>(host, std::move(session), strict_handles)) {}
ReaperApiCore::~ReaperApiCore() = default;
void ReaperApiCore::observe_project() { project_ = host_.current_project(); }
void ReaperApiCore::reset_handles() { native_->reset(); project_ = nullptr; }
Json ReaperApiCore::api_capabilities() const { return native_->capabilities(); }
Json ReaperApiCore::project_handle(void* project) { return native_->handle(project, "ReaProject"); }
Json ReaperApiCore::call(const std::string& name, const Json& args) {
  const auto& entries = native_entries();
  auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return name == item.name; });
  if (entry == entries.end()) throw Error("UNKNOWN_API", "API is not registered in the REAPER API catalogue");
  if (!args.is_array() || args.size() < size_t(entry->min_args) || args.size() > size_t(entry->max_args))
    throw Error("INVALID_ARGUMENT", "Wrong argument count");
  observe_project();
  return native_->invoke(*entry, args);
}
}
