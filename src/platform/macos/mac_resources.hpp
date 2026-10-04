#pragma once
#include "web/web_resources.hpp"
#import <WebKit/WebKit.h>

@interface ReaWebSchemeHandler : NSObject <WKURLSchemeHandler> {
@public
  std::shared_ptr<reaweb::WebResources> resources;
}
@end
@implementation ReaWebSchemeHandler
- (void)webView:(WKWebView*)view startURLSchemeTask:(id<WKURLSchemeTask>)task {
  (void)view;
  std::map<std::string, std::string> headers;
  for (NSString* key in task.request.allHTTPHeaderFields)
    headers[key.lowercaseString.UTF8String] = [task.request.allHTTPHeaderFields[key] UTF8String];
  auto response = resources->request(task.request.URL.absoluteString.UTF8String ?: "", task.request.HTTPMethod.UTF8String ?: "GET", headers);
  auto fields = [NSMutableDictionary dictionary];
  for (const auto& [name, value] : response.headers)
    fields[[NSString stringWithUTF8String:name.c_str()]] = [NSString stringWithUTF8String:value.c_str()];
  auto native = [[NSHTTPURLResponse alloc] initWithURL:task.request.URL statusCode:response.status HTTPVersion:@"HTTP/1.1" headerFields:fields];
  [task didReceiveResponse:native];
  if (response.size) {
    const auto owner = response.owner;
    auto data = [[NSData alloc] initWithBytesNoCopy:const_cast<char*>(response.data) length:response.size
      deallocator:^(void*, NSUInteger) { (void)owner; }];
    [task didReceiveData:data];
  }
  [task didFinish];
}
- (void)webView:(WKWebView*)view stopURLSchemeTask:(id<WKURLSchemeTask>)task { (void)view; (void)task; }
@end
