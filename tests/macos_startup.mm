// Opt-in first-visible-frame acceptance in an isolated REAPER profile.
#include <reaper_plugin.h>
#include "platform/platform.hpp"
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <fstream>

namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
int id = 0, samples = 0, variant = 0;
DWORD started;
void tick();
void finish(const std::string& text) {
  std::ofstream(resource / "startup-test.log", std::ios::app) << text << '\n';
  host->Register("-timer", reinterpret_cast<void*>(tick));
  PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
}
WKWebView* find_view() {
  for (NSWindow* window in NSApp.windows) {
    std::vector<NSView*> queue{window.contentView};
    while (!queue.empty()) {
      auto view = queue.back(); queue.pop_back();
      if ([view isKindOfClass:WKWebView.class]) return (WKWebView*)view;
      for (NSView* child in view.subviews) queue.push_back(child);
    }
  }
  return nil;
}
void tick() {
  try {
    if (GetTickCount() - started > 20000) throw std::runtime_error("First visible frame timed out");
    if (NSApp.modalWindow) {
      std::vector<NSView*> queue{NSApp.modalWindow.contentView};
      while (!queue.empty()) {
        auto view = queue.back(); queue.pop_back();
        if ([view isKindOfClass:NSButton.class] && [[(NSButton*)view title] isEqualToString:@"No"])
          [(NSButton*)view performClick:nil];
        for (NSView* child in view.subviews) queue.push_back(child);
      }
      return;
    }
    if (!IsWindowVisible(host->hwnd_main)) return;
    if (!id) {
      if (!host->GetFunc("ReaWeb_Open")) return;
      if (variant == 3) {
        std::string url; std::ifstream(resource / "startup-test.url") >> url;
        id = reinterpret_cast<int(*)(const char*)>(host->GetFunc("ReaWeb_OpenDev"))(url.c_str());
      } else {
        id = reinterpret_cast<int(*)(const char*, const char*, const char*, const bool*)>(host->GetFunc("ReaWeb_Open"))(
          (resource / "Scripts/startup-test/index.html").c_str(), "startup-test.lua", nullptr, nullptr);
      }
      if (!id) throw std::runtime_error(reinterpret_cast<const char*(*)()>(host->GetFunc("ReaWeb_GetLastError"))());
      std::ofstream(resource / "startup-test.log", std::ios::app) << "opened=" << id << '\n';
    }
    auto view = find_view();
    if (!view || !view.window.visible || view.hiddenOrHasHiddenAncestor) return;
    using Capture = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
    static auto capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
    if (!capture) throw std::runtime_error("Native capture unavailable");
    auto image = capture(CGRectNull, kCGWindowListOptionIncludingWindow, CGWindowID(view.window.windowNumber), kCGWindowImageBoundsIgnoreFraming);
    if (!image) return; // The window server has not published this newly ordered window yet.
    auto bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image]; CGImageRelease(image);
    const auto rect = [view convertRect:view.bounds toView:nil];
    const double scale = double(bitmap.pixelsWide) / view.window.frame.size.width;
    const int left = int(rect.origin.x * scale), top = int((view.window.frame.size.height - NSMaxY(rect)) * scale);
    auto marker = [[bitmap colorAtX:left + int(30 * scale) y:top + int(30 * scale)] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    auto background = [[bitmap colorAtX:left + int(160 * scale) y:top + int(160 * scale)] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    const bool green = marker.greenComponent > .7 && marker.greenComponent - marker.redComponent > .3 &&
      marker.greenComponent - marker.blueComponent > .3;
    const bool white = background.redComponent > .95 && background.greenComponent > .95 && background.blueComponent > .95;
    if (!green || white) {
      [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:[NSString stringWithUTF8String:(resource / "startup-failure.png").c_str()] atomically:YES];
      throw std::runtime_error("First visible native frame is blank or missing page content");
    }
    if (++samples == 20) finish("PASS: first-visible native content, samples=" + std::to_string(samples) +
      " elapsedMs=" + std::to_string(GetTickCount() - started));
  } catch (const std::exception& error) { finish(std::string("FAIL: ") + error.what()); }
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "startup-test.enabled")) return 0;
  std::ifstream(resource / "startup-test.enabled") >> variant;
  const auto page = resource / "Scripts/startup-test/index.html"; fs::create_directories(page.parent_path());
  const auto background = variant == 1 ? "#e8d8c8" : variant == 2 ? "linear-gradient(120deg,#182838,#486878)" : "#182838";
  std::ofstream(page) << "<!doctype html><style>html{min-height:100%;background:" << background
    << "}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>";
  started = GetTickCount(); host->Register("timer", reinterpret_cast<void*>(tick)); return 1;
}
