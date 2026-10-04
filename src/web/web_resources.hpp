#pragma once
#include "core/core.hpp"

namespace reaweb {
struct ResourceResponse {
  int status = 200;
  std::map<std::string, std::string> headers;
  // Retain the mapped file until the native WebView has consumed the response.
  std::shared_ptr<const void> owner;
  const char* data = nullptr;
  size_t size = 0;
  const char* reason() const {
    switch (status) {
      case 200: return "OK";
      case 206: return "Partial Content";
      case 304: return "Not Modified";
      case 400: return "Bad Request";
      case 403: return "Forbidden";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 416: return "Range Not Satisfiable";
      default: return "Internal Server Error";
    }
  }
};
class WebResources {
public:
  WebResources(const fs::path& root, const std::string& app_id);
  const fs::path& root() const { return root_; }
  const std::string& origin() const { return origin_; }
  std::string entry_url(const fs::path& entry) const;
  ResourceResponse request(const std::string& uri, const std::string& method,
    const std::map<std::string, std::string>& headers = {}) const;
private:
  fs::path root_;
  std::string origin_;
};
std::string app_identity(const fs::path& root, const std::string& launcher_source = {});
void bind_app_identity(const fs::path& root, const fs::path& profile, const std::string& app_id);
}
