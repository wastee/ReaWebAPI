#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace reaweb {
using Json = nlohmann::json;
using Guid = std::array<unsigned char, 16>;
namespace fs = std::filesystem;
inline constexpr size_t message_limit = 64 * 1024 * 1024;
inline constexpr size_t value_limit = 16 * 1024 * 1024;
inline constexpr size_t batch_limit = 128;
inline constexpr size_t host_message_limit = 1024 * 1024;
inline constexpr size_t host_queue_limit = 256;
inline constexpr size_t host_queue_bytes = value_limit;

struct Error : std::runtime_error {
  std::string code;
  Json details = nullptr;
  Error(std::string code, const std::string& message, Json details = nullptr)
    : std::runtime_error(message), code(std::move(code)), details(std::move(details)) {}
};

struct Host {
  std::function<void*(const char*)> native_function;
  std::function<bool(void*)> valid_window;
  std::function<void*()> current_project;
  std::function<int(void*)> count_selected_tracks;
  std::function<void*(void*, int)> get_selected_track;
  std::function<bool(void*, void*)> valid_track;
  std::function<Guid(void*)> track_guid;
  std::function<int(void*)> change_count;
  std::function<uint64_t()> project_generation;
  std::function<void(void*)> begin_undo;
  std::function<void(void*, const std::string&)> end_undo;
  std::function<void(int)> prevent_refresh;
  std::function<void()> update_arrange;
  std::function<int(void*)> count_selected_items;
  std::function<std::pair<std::string, std::string>(void*, int)> item_identity;
  std::function<Json(const std::string&)> event_snapshot;
  std::function<int()> track_count;
  std::function<std::string(int)> track_identity;
  std::function<uint64_t(const std::string&)> event_revision;
  std::function<Json()> project_save_state;
};

fs::path resolve_html(const fs::path& base, const std::string& input);
std::string file_uri(const fs::path& path);
bool same_document(const std::string& uri, const std::string& entry);
std::string validate_dev_url(const std::string& url);
void validate_external_url(const std::string& url);
Json encode_binary(const char* data, size_t size);
std::string decode_binary(const Json& value);
Json parse_request(const std::string& message);
Json error_response(const Json& request, const std::string& code, const std::string& message, Json details = nullptr);

// ReaWebAPI-owned API core, shared by WebView and External Session consumers.
class ReaperApiCore {
public:
  ReaperApiCore(Host& host, std::string session, bool strict_handles = false);
  ~ReaperApiCore();
  Json call(const std::string& name, const Json& args);
  Json batch(const Json& args);
  Json api_capabilities() const;
  Json project_handle(void* project);
  void observe_project();
  void reset_handles();
  void validate_managed_call(const std::string& method, const Json& args);
protected:
  Host& host_;
  std::unique_ptr<class NativeContext> native_;
  void* project_ = nullptr;
  bool batching_ = false;
};

class Bridge : public ReaperApiCore {
public:
  struct Controls {
    std::function<int(const std::string&)> open;
    std::function<void()> close;
    std::function<void()> devtools;
    std::function<bool(bool)> set_docked;
    std::function<bool()> is_docked;
    std::function<Json(const std::string&, const Json&)> host_call;
  };
  Bridge(Host& host, Controls controls, std::string session);
  ~Bridge();
  Json dispatch(const std::string& message);
  Json dispatch_request(const Json& request);
private:
  struct Method { size_t min_args, max_args; std::function<Json(const Json&)> invoke; };
  Controls controls_;
  std::string session_;
  std::map<std::string, Method> methods_;
  void add(const std::string& name, size_t min, size_t max, std::function<Json(const Json&)> fn);
  void add_reaper(const std::string& name, std::function<Json(const Json&)> fn);
};
}
