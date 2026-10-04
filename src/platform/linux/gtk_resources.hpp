#pragma once
#include "web/web_resources.hpp"
#include <webkit2/webkit2.h>

namespace reaweb {
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
    auto bytes = g_bytes_new_with_free_func(response->data ? response->data : "", response->size,
      +[](gpointer value) { delete static_cast<ResourceResponse*>(value); }, response);
    auto stream = g_memory_input_stream_new_from_bytes(bytes);
    auto native = webkit_uri_scheme_response_new(stream, response->size);
    webkit_uri_scheme_response_set_status(native, response->status, response->reason());
    auto native_headers = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    for (const auto& [name, value] : response->headers) soup_message_headers_append(native_headers, name.c_str(), value.c_str());
    webkit_uri_scheme_response_set_http_headers(native, native_headers);
    webkit_uri_scheme_response_set_content_type(native, response->headers.at("Content-Type").c_str());
    webkit_uri_scheme_request_finish_with_response(request, native);
    g_object_unref(native); g_object_unref(stream); g_bytes_unref(bytes);
  }, nullptr, nullptr);
}
}
