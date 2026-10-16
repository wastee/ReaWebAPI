#pragma once
#include "web/web_resources.hpp"
#include <libsoup/soup.h>
#include <array>

namespace reaweb {
// GStreamer's WebKit source accepts HTTP(S)/Blob, but not custom URI schemes.
// This listener belongs to one page and only serves its opaque media grants.
class GtkMedia {
  std::shared_ptr<WebResources> resources_;
  SoupServer* server_ = nullptr;
  std::string origin_, authority_;
  struct Transfer { ResourceResponse response; uint64_t position = 0; };
  static void next(SoupServerMessage* message, gpointer data) {
    auto& transfer = *static_cast<Transfer*>(data);
    auto body = soup_server_message_get_response_body(message);
    try {
      const auto left = transfer.response.size - transfer.position;
      if (!left) { soup_message_body_complete(body); return; }
      std::array<char, 65536> bytes;
      const auto count = transfer.response.reader->read(transfer.response.offset + transfer.position,
        bytes.data(), static_cast<size_t>(std::min<uint64_t>(bytes.size(), left)));
      if (!count) throw Error("MEDIA_CLOSED", "Unexpected end of media");
      transfer.position += count;
      soup_message_body_append(body, SOUP_MEMORY_COPY, bytes.data(), count);
      if (transfer.position == transfer.response.size) soup_message_body_complete(body);
    } catch (...) {
      // Do not turn a revoked/truncated stream into a successful short response.
      if (auto socket = soup_server_message_get_socket(message)) g_socket_close(socket, nullptr);
    }
  }
  void authorize(SoupServerMessage* message) {
    auto headers = soup_server_message_get_request_headers(message);
    auto value = [&](const char* name) { const auto text = soup_message_headers_get_one(headers, name); return std::string(text ? text : ""); };
    const auto origin = value("Origin"), referrer = value("Referer");
    // The URL is a bearer grant. Reject web origins other than the owning App,
    // including null origins and DNS rebinding to this loopback port.
    if (value("Host") != authority_ ||
        (!origin.empty() ? origin != resources_->origin() : referrer.compare(0, resources_->origin().size() + 1, resources_->origin() + "/") != 0)) {
      soup_server_message_set_status(message, 403, nullptr); return;
    }
    auto response = soup_server_message_get_response_headers(message);
    soup_message_headers_replace(response, "Access-Control-Allow-Origin", resources_->origin().c_str());
    soup_message_headers_replace(response, "Access-Control-Expose-Headers", "Content-Length, Content-Range, Accept-Ranges");
    soup_message_headers_replace(response, "Vary", "Origin");
    const std::string method = soup_server_message_get_method(message);
    if (method == "OPTIONS") {
      soup_message_headers_replace(response, "Access-Control-Allow-Methods", "GET, HEAD");
      soup_message_headers_replace(response, "Access-Control-Allow-Headers", "Range, If-Range");
      soup_server_message_set_status(message, 204, nullptr);
    } else if (method != "GET" && method != "HEAD") {
      soup_message_headers_replace(response, "Allow", "GET, HEAD, OPTIONS");
      soup_server_message_set_status(message, 405, nullptr);
    } else if (soup_message_headers_get_content_length(headers) || !value("Transfer-Encoding").empty()) {
      soup_server_message_set_status(message, 400, nullptr);
    }
  }
  void respond(SoupServerMessage* message, const char* path) {
    if (soup_server_message_get_status(message)) return;
    const std::string media_prefix = "/__reaweb_media__/";
    const std::string requested = path ? path : "";
    if (requested.size() != media_prefix.size() + 64 || requested.compare(0, media_prefix.size(), media_prefix) ||
        requested.find_first_not_of("0123456789abcdef", media_prefix.size()) != std::string::npos) {
      soup_server_message_set_status(message, 404, nullptr); return;
    }
    std::map<std::string, std::string> headers;
    auto incoming = soup_server_message_get_request_headers(message);
    for (const auto name : {"range", "if-range"})
      if (const auto value = soup_message_headers_get_one(incoming, name)) headers[name] = value;
    auto response = resources_->request(resources_->origin() + requested, soup_server_message_get_method(message), headers);
    soup_server_message_set_status(message, response.status, response.reason());
    auto outgoing = soup_server_message_get_response_headers(message);
    for (const auto& [name, value] : response.headers)
      if (name != "Cross-Origin-Resource-Policy") soup_message_headers_replace(outgoing, name.c_str(), value.c_str());
    if (!response.reader) return;
    auto transfer = new Transfer{std::move(response)};
    g_object_set_data_full(G_OBJECT(message), "reaweb-media-transfer", transfer,
      +[](gpointer data) { delete static_cast<Transfer*>(data); });
    // Discard sent chunks. The total file size must never become a body buffer.
    soup_message_body_set_accumulate(soup_server_message_get_response_body(message), FALSE);
    g_signal_connect(message, "wrote-chunk", G_CALLBACK(next), transfer);
    next(message, transfer);
  }
public:
  explicit GtkMedia(std::shared_ptr<WebResources> resources) : resources_(std::move(resources)) {
    server_ = soup_server_new("raw-paths", TRUE, nullptr);
    auto address = g_inet_socket_address_new_from_string("127.0.0.1", 0);
    GError* error = nullptr;
    const bool listening = soup_server_listen(server_, address, static_cast<SoupServerListenOptions>(0), &error);
    g_object_unref(address);
    if (!listening) {
      std::string message = error ? error->message : "Cannot start local media listener";
      g_clear_error(&error); g_clear_object(&server_); throw Error("HOST_UNAVAILABLE", message);
    }
    auto uris = soup_server_get_uris(server_);
    authority_ = "127.0.0.1:" + std::to_string(g_uri_get_port(static_cast<GUri*>(uris->data)));
    origin_ = "http://" + authority_;
    g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    soup_server_add_early_handler(server_, nullptr, +[](SoupServer*, SoupServerMessage* message, const char*, GHashTable*, gpointer data) {
      static_cast<GtkMedia*>(data)->authorize(message);
    }, this, nullptr);
    soup_server_add_handler(server_, nullptr, +[](SoupServer*, SoupServerMessage* message, const char* path, GHashTable*, gpointer data) {
      try { static_cast<GtkMedia*>(data)->respond(message, path); }
      catch (...) { soup_server_message_set_status(message, 500, nullptr); }
    }, this, nullptr);
  }
  ~GtkMedia() { soup_server_disconnect(server_); g_object_unref(server_); }
  GtkMedia(const GtkMedia&) = delete;
  GtkMedia& operator=(const GtkMedia&) = delete;
  Json open(const fs::path& path) {
    auto media = resources_->open_media(path);
    media["url"] = origin_ + media.at("url").get<std::string>().substr(resources_->origin().size());
    return media;
  }
  bool close(const std::string& url) {
    if (url.compare(0, origin_.size() + 1, origin_ + "/") != 0)
      throw Error("INVALID_ARGUMENT", "Expected a media URL owned by this document");
    return resources_->close_media(resources_->origin() + url.substr(origin_.size()));
  }
};
}
