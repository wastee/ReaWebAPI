#include "platform/linux/gtk_media.hpp"
#include <httplib.h>
#include <fstream>
#include <future>
#include <iostream>

using namespace reaweb;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  const auto root = fs::temp_directory_path() / ("reaweb-http-media-" + std::to_string(g_get_monotonic_time()));
  fs::create_directories(root);
  try {
    const auto path = root / "large.wav";
    { std::ofstream file(path, std::ios::binary); file << "RIFFtest"; file.seekp(20 * 1024 * 1024); file.put('Z'); }
    auto resources = std::make_shared<WebResources>(root, "media-test");
    GtkMedia media(resources);
    GtkMedia second(std::make_shared<WebResources>(root, "media-test"));
    const auto second_url = second.open(path).at("url").get<std::string>();
    const auto url = media.open(path).at("url").get<std::string>();
    const auto split = url.find('/', 7);
    const auto endpoint = url.substr(split);
    auto checks = std::async(std::launch::async, [&] {
      httplib::Client client(url.substr(0, split));
      client.set_read_timeout(5); client.set_connection_timeout(5);
      const httplib::Headers headers{{"Origin", resources->origin()}};
      auto head = client.Head(endpoint, headers);
      require(head && head->status == 200 && head->get_header_value("Content-Length") == "20971521", "HEAD length");
      auto range_headers = headers; range_headers.emplace("Range", "bytes=0-7");
      auto range = client.Get(endpoint, range_headers);
      require(range && range->status == 206 && range->body == "RIFFtest" && range->get_header_value("Content-Range") == "bytes 0-7/20971521", "range body");
      auto suffix_headers = headers; suffix_headers.emplace("Range", "bytes=-1");
      auto suffix = client.Get(endpoint, suffix_headers);
      require(suffix && suffix->status == 206 && suffix->body == "Z", "suffix range");
      auto invalid_headers = headers; invalid_headers.emplace("Range", "bytes=999999999-");
      auto invalid = client.Get(endpoint, invalid_headers);
      require(invalid && invalid->status == 416 && invalid->get_header_value("Content-Range") == "bytes */20971521", "unsatisfiable range");
      auto full = client.Get(endpoint, headers);
      require(full && full->status == 200 && full->body.size() == 20971521 && full->body.back() == 'Z', "bounded full stream");
      auto foreign = client.Get(endpoint, httplib::Headers{{"Origin", "https://example.com"}});
      require(foreign && foreign->status == 403, "foreign origin denied");
      auto missing = client.Get(endpoint);
      require(missing && missing->status == 403, "anonymous caller denied");
      auto host_headers = headers; host_headers.emplace("Host", "evil.example");
      auto rebound = client.Get(endpoint, host_headers);
      require(rebound && rebound->status == 403, "foreign host denied");
      auto static_file = client.Get("/large.wav", headers);
      require(static_file && static_file->status == 404, "no filesystem/static route");
      auto preflight = client.Options(endpoint, headers);
      require(preflight && preflight->status == 204 && preflight->get_header_value("Access-Control-Allow-Origin") == resources->origin(), "CORS preflight");
      auto post = client.Post(endpoint, headers, "", "text/plain");
      require(post && post->status == 405, "read-only endpoint");
      require(media.close(url) && !media.close(url), "idempotent release");
      auto revoked = client.Get(endpoint, headers);
      require(revoked && revoked->status == 404, "released grant denied");
      const auto again = media.open(path).at("url").get<std::string>();
      resources->clear_media();
      auto cleared = client.Get(again.substr(split), headers);
      require(cleared && cleared->status == 404, "document revocation");
      const auto streaming = media.open(path).at("url").get<std::string>();
      size_t received = 0;
      auto interrupted = client.Get(streaming.substr(split), headers, [&](const char*, size_t size) {
        if (!received) resources->clear_media();
        received += size; return true;
      });
      require(!interrupted && received < 20971521, "in-flight reader revocation");
      const auto changed = media.open(path).at("url").get<std::string>();
      { std::ofstream file(path, std::ios::app); file.put('x'); }
      auto stale = client.Get(changed.substr(split), headers);
      require(stale && stale->status == 404, "changed file rejected");
      httplib::Client other(second_url.substr(0, second_url.find('/', 7)));
      auto cross_window = other.Get(endpoint, headers);
      require(cross_window && cross_window->status == 404, "grants not registered in another page listener");
      try { second.close(url); require(false, "foreign close accepted"); }
      catch (const Error& error) { require(error.code == "INVALID_ARGUMENT", "foreign close error"); }
    });
    const auto deadline = g_get_monotonic_time() + 30000000;
    while (checks.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      while (g_main_context_iteration(nullptr, FALSE)) {}
      if (g_get_monotonic_time() > deadline) throw std::runtime_error("media server timeout");
      g_usleep(1000);
    }
    checks.get();
  } catch (...) { fs::remove_all(root); throw; }
  fs::remove_all(root);
  std::cout << "Linux media streaming, ranges, origin checks and revocation passed\n";
}
