#include "web/web_resources.hpp"
#include <fstream>
#include <iostream>
#include <chrono>

using namespace reaweb;
static void require(bool value) { if (!value) throw std::runtime_error("Media resource assertion failed"); }
int main() {
  const auto root = fs::temp_directory_path() / ("reaweb-media-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root / "app");
  try {
    const auto path = root / fs::u8path("outside #%.wav");
    { std::ofstream file(path, std::ios::binary); file << "RIFFtest"; file.seekp(20 * 1024 * 1024); file.put('Z'); }
    WebResources first(root / "app", "media-test"), second(root / "app", "media-test"), other(root / "app", "other");
    const auto media = first.open_media(path);
    const auto url = media.at("url").get<std::string>();
    require(url.find("outside") == std::string::npos && media.at("size") == 20 * 1024 * 1024 + 1);
    require(second.request(url, "GET").status == 404 && other.request(url, "GET").status == 403);
    require(first.request(url, "POST").status == 405);
    require(first.request(url, "GET", {{"origin", "https://evil.example"}}).status == 403);
    const auto head = first.request(url, "HEAD");
    require(head.status == 200 && !head.reader && head.size == 0 && head.headers.at("Content-Length") == "20971521");
    auto range = first.request(url, "GET", {{"range", "bytes=0-7"}});
    require(range.status == 206 && range.size == 8 && range.headers.at("Content-Range") == "bytes 0-7/20971521");
    char output[65537]{};
    require(range.reader->read(range.offset, output, range.size) == 8 && std::string(output, 8) == "RIFFtest");
    auto tail = first.request(url, "GET", {{"range", "bytes=-1"}});
    require(tail.reader->read(tail.offset, output, 1) == 1 && output[0] == 'Z');
    auto full = first.request(url, "GET");
    require(full.reader->read(0, output, sizeof(output)) == 65536);
    require(first.request(url, "GET", {{"range", "bytes=999999999-"}}).status == 416);
    require(first.request(url, "GET", {{"range", "bytes=0-7"}, {"if-range", "\"unknown\""}}).status == 200);
    require(first.close_media(url) && !first.close_media(url));
    require(first.request(url, "GET").status == 404);
    try { range.reader->read(0, output, 8); require(false); } catch (const Error& e) { require(e.code == "MEDIA_CLOSED"); }
    for (int i = 0; i < 32; ++i) first.open_media(path);
    try { first.open_media(path); require(false); } catch (const Error& e) { require(e.code == "MEDIA_LIMIT"); }
    first.clear_media();
    auto changed = first.open_media(path).at("url").get<std::string>();
    { std::ofstream file(path, std::ios::app); file.put('x'); }
    require(first.request(changed, "GET").status == 404);
    first.clear_media();
    range = {}; tail = {}; full = {};
    const auto bounded = first.open_media(path).at("url").get<std::string>();
    std::vector<ResourceResponse> responses;
    for (int i = 0; i < 64; ++i) {
      responses.push_back(first.request(bounded, "GET"));
      require(responses.back().status == 200);
    }
    require(first.request(bounded, "GET").status == 429);
    require(first.request(bounded, "HEAD").status == 200);
    responses.pop_back();
    require(first.request(bounded, "GET").status == 200);
    fs::remove(path);
    require(first.request(bounded, "GET").status == 404);
    try { responses.front().reader->read(0, output, 8); require(false); }
    catch (const Error& e) { require(e.code == "MEDIA_CLOSED"); }
    responses.clear(); first.clear_media();
    std::ofstream(root / "evil.html") << "<script>alert(1)</script>";
    try { first.open_media(root / "evil.html"); require(false); } catch (const Error& e) { require(e.code == "UNSUPPORTED_FORMAT"); }
  } catch (...) { fs::remove_all(root); throw; }
  fs::remove_all(root);
  std::cout << "Media isolation, bounded reads, ranges, limits and revocation passed\n";
}
