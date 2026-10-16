#pragma once
#include "web/web_resources.hpp"
#include <webkit2/webkit2.h>

namespace reaweb {
struct MediaInput { GInputStream parent; ResourceResponse* response; uint64_t position; };
struct MediaInputClass { GInputStreamClass parent; };
G_DEFINE_TYPE(MediaInput, media_input, G_TYPE_INPUT_STREAM)
static gssize media_input_read(GInputStream* input, void* buffer, gsize count, GCancellable* cancel, GError** error) {
  if (g_cancellable_set_error_if_cancelled(cancel, error)) return -1;
  auto self = reinterpret_cast<MediaInput*>(input);
  try {
    const auto count_left = self->response->size - std::min<uint64_t>(self->position, self->response->size);
    const auto size = self->response->reader->read(self->response->offset + self->position, static_cast<char*>(buffer),
      static_cast<size_t>(std::min<uint64_t>(count, count_left)));
    self->position += size; return static_cast<gssize>(size);
  } catch (const std::exception& failure) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CLOSED, failure.what()); return -1; }
}
static void media_input_finalize(GObject* object) {
  delete reinterpret_cast<MediaInput*>(object)->response;
  G_OBJECT_CLASS(media_input_parent_class)->finalize(object);
}
static void media_input_class_init(MediaInputClass* klass) {
  G_INPUT_STREAM_CLASS(klass)->read_fn = media_input_read;
  G_OBJECT_CLASS(klass)->finalize = media_input_finalize;
}
static void media_input_init(MediaInput* input) { input->response = nullptr; input->position = 0; }
inline void register_app_resources(WebKitWebContext* context) {
  auto security = webkit_web_context_get_security_manager(context);
  webkit_security_manager_register_uri_scheme_as_secure(security, "reaweb");
  webkit_security_manager_register_uri_scheme_as_cors_enabled(security, "reaweb");
  webkit_web_context_register_uri_scheme(context, "reaweb", +[](WebKitURISchemeRequest* request, gpointer) {
    auto view = webkit_uri_scheme_request_get_web_view(request);
    auto resources = view ? static_cast<WebResources*>(g_object_get_data(G_OBJECT(view), "reaweb-resources")) : nullptr;
    if (!resources) {
      auto error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "App resource context unavailable");
      webkit_uri_scheme_request_finish_error(request, error); g_error_free(error); return;
    }
    std::map<std::string, std::string> headers;
    auto request_headers = webkit_uri_scheme_request_get_http_headers(request);
    for (const auto* name : {"origin", "sec-fetch-site", "range", "if-range", "if-none-match", "if-modified-since"})
      if (const auto* value = soup_message_headers_get_one(request_headers, name)) headers[name] = value;
    auto response = new ResourceResponse(resources->request(webkit_uri_scheme_request_get_uri(request),
      webkit_uri_scheme_request_get_http_method(request), headers));
    GInputStream* stream;
    if (response->reader) {
      auto input = reinterpret_cast<MediaInput*>(g_object_new(media_input_get_type(), nullptr));
      input->response = response; stream = G_INPUT_STREAM(input);
    } else {
      auto bytes = g_bytes_new_with_free_func(response->data ? response->data : "", response->size,
        +[](gpointer value) { delete static_cast<ResourceResponse*>(value); }, response);
      stream = g_memory_input_stream_new_from_bytes(bytes); g_bytes_unref(bytes);
    }
    auto native = webkit_uri_scheme_response_new(stream, response->size);
    webkit_uri_scheme_response_set_status(native, response->status, response->reason());
    auto native_headers = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    for (const auto& [name, value] : response->headers) soup_message_headers_append(native_headers, name.c_str(), value.c_str());
    webkit_uri_scheme_response_set_http_headers(native, native_headers);
    webkit_uri_scheme_response_set_content_type(native, response->headers.at("Content-Type").c_str());
    webkit_uri_scheme_request_finish_with_response(request, native);
    g_object_unref(native); g_object_unref(stream);
  }, nullptr, nullptr);
}
}
