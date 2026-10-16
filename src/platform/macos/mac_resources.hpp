#pragma once
#include "web/web_resources.hpp"
#import <WebKit/WebKit.h>

@interface ReaWebSchemeHandler : NSObject <WKURLSchemeHandler> {
@public
  std::shared_ptr<reaweb::WebResources> resources;
  NSMutableSet* mediaTasks;
}
@end
@implementation ReaWebSchemeHandler
- (void)readMedia:(std::shared_ptr<reaweb::ResourceResponse>)response task:(id<WKURLSchemeTask>)task position:(uint64_t)position {
  if (![mediaTasks containsObject:task]) return;
  if (position >= response->size) { [mediaTasks removeObject:task]; [task didFinish]; return; }
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    auto bytes = std::make_shared<std::vector<char>>(65536);
    size_t count = 0;
    std::string failure;
    try { count = response->reader->read(response->offset + position, bytes->data(), std::min<size_t>(bytes->size(), response->size - position)); }
    catch (const std::exception& error) { failure = error.what(); }
    dispatch_async(dispatch_get_main_queue(), ^{
      if (![self->mediaTasks containsObject:task]) return;
      if (!failure.empty() || !count) {
        [self->mediaTasks removeObject:task];
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotOpenFile userInfo:nil]];
        return;
      }
      [task didReceiveData:[NSData dataWithBytes:bytes->data() length:count]];
      [self readMedia:response task:task position:position + count];
    });
  });
}
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
  if (response.reader) {
    if (!mediaTasks) mediaTasks = [NSMutableSet set];
    [mediaTasks addObject:task];
    [self readMedia:std::make_shared<reaweb::ResourceResponse>(std::move(response)) task:task position:0];
    return;
  }
  if (response.size) {
    const auto owner = response.owner;
    auto data = [[NSData alloc] initWithBytesNoCopy:const_cast<char*>(response.data) length:response.size
      deallocator:^(void*, NSUInteger) { (void)owner; }];
    [task didReceiveData:data];
  }
  [task didFinish];
}
- (void)webView:(WKWebView*)view stopURLSchemeTask:(id<WKURLSchemeTask>)task { (void)view; [mediaTasks removeObject:task]; }
@end
