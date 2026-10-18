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
int hidden_stage = 0;
int hidden_cycles = 0;
DWORD hidden_ready = 0;
__weak NSWindow* hidden_key;
void tick();
bool page_painted(WKWebView* view) {
  using Capture = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
  static auto capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
  if (!capture) throw std::runtime_error("Native capture unavailable");
  auto image = capture(CGRectNull, kCGWindowListOptionIncludingWindow, CGWindowID(view.window.windowNumber), kCGWindowImageBoundsIgnoreFraming);
  if (!image) return false;
  auto bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image]; CGImageRelease(image);
  const auto rect = [view convertRect:view.bounds toView:nil];
  const double scale = double(bitmap.pixelsWide) / view.window.frame.size.width;
  const int left = int(rect.origin.x * scale), top = int((view.window.frame.size.height - NSMaxY(rect)) * scale);
  auto marker = [[bitmap colorAtX:left + int(30 * scale) y:top + int(30 * scale)] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  const bool green = marker.greenComponent > .7 && marker.greenComponent - marker.redComponent > .3 &&
    marker.greenComponent - marker.blueComponent > .3;
  if (!green) [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
    writeToFile:[NSString stringWithUTF8String:(resource / "startup-failure.png").c_str()] atomically:YES];
  return green;
}
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
void hidden_tick() {
  const auto open = reinterpret_cast<int(*)(const char*, const char*)>(host->GetFunc("ReaWeb_OpenEx"));
  const auto close = reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Close"));
  const auto path = (resource / "Scripts/startup-test/index.html").string();
  if (!id) {
    hidden_key = NSApp.keyWindow;
    id = open(path.c_str(), variant == 6 ? "{\"instanceKey\":\"startup-test.lua\",\"hidden\":true,\"frameless\":true}" :
      "{\"instanceKey\":\"startup-test.lua\",\"hidden\":true}");
    if (!id) throw std::runtime_error("Hidden OpenEx failed");
  }
  auto view = find_view();
  if (hidden_stage == 0) {
    if (view && view.window.visible) throw std::runtime_error("Hidden window was ordered on screen");
    if (NSApp.keyWindow != hidden_key) throw std::runtime_error("Hidden startup changed the key window");
    const auto receive = reinterpret_cast<const char*(*)(int)>(host->GetFunc("ReaWeb_Receive"));
    if (std::string(receive(id)) == "hidden-ready") hidden_ready = GetTickCount();
    if (!hidden_ready || GetTickCount() - hidden_ready < 1000) return;
    const bool active = NSApp.active;
    hidden_key = NSApp.keyWindow;
    if (!reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Show"))(id))
      throw std::runtime_error("Show failed");
    if (!view.window.visible || NSApp.active != active || NSApp.keyWindow != hidden_key)
      throw std::runtime_error("Show did not preserve key window");
    hidden_stage = 5;
  } else if (hidden_stage == 5) {
    if (NSApp.keyWindow != hidden_key) throw std::runtime_error("First Show changed the key window");
    if (!page_painted(view)) return;
    if (!reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Focus"))(id))
      throw std::runtime_error("Focus failed");
    hidden_stage = 4;
  } else if (hidden_stage == 4) {
    auto responder = view.window.firstResponder;
    if (!NSApp.active || !view.window.keyWindow || ![responder isKindOfClass:NSView.class] ||
        ![(NSView*)responder isDescendantOf:view]) return;
    [view evaluateJavaScript:@"reaper.window.hide().then(() => reaper.host.send('hidden-again'))" completionHandler:nil];
    hidden_stage = 1;
  } else if (hidden_stage == 1) {
    const auto receive = reinterpret_cast<const char*(*)(int)>(host->GetFunc("ReaWeb_Receive"));
    if (std::string(receive(id)) != "hidden-again") return;
    if (view.window.visible) throw std::runtime_error("Hide did not hide the native window");
    if (hidden_cycles < 3) {
      [view.window setContentSize:NSMakeSize(600 + hidden_cycles * 40, 420 + hidden_cycles * 30)];
      if (!reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Show"))(id))
        throw std::runtime_error("Repeated Show failed");
      hidden_stage = 6;
      return;
    }
    if (!close(id)) throw std::runtime_error("Hidden close failed");
    hidden_stage = 2;
  } else if (hidden_stage == 6) {
    if (!view.window.visible || !page_painted(view)) return;
    [view.window setContentSize:NSMakeSize(640 + hidden_cycles * 40, 450 + hidden_cycles * 30)];
    hidden_stage = 7;
  } else if (hidden_stage == 7) {
    if (!page_painted(view)) return;
    ++hidden_cycles;
    [view evaluateJavaScript:@"reaper.window.hide().then(() => reaper.host.send('hidden-again'))" completionHandler:nil];
    hidden_stage = 1;
  } else if (hidden_stage == 2) {
    if (reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsOpen"))(id)) return;
    id = open(path.c_str(), "{\"instanceKey\":\"startup-test.lua\",\"hidden\":true}");
    if (!id || !close(id)) throw std::runtime_error("Close before initialization failed");
    hidden_ready = GetTickCount();
    hidden_stage = 3;
  } else {
    if (view && view.window.visible) throw std::runtime_error("Closed hidden window became visible");
    if (GetTickCount() - hidden_ready < 1000) return;
    if (reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsOpen"))(id))
      throw std::runtime_error("Closed hidden window remained open");
    finish("PASS: hidden native startup, first Show pixels without focus, Focus, repeated Hide/Show, resize and early Close");
  }
}
void tick() {
  try {
    if (GetTickCount() - started > 20000) {
      auto view = find_view();
      throw std::runtime_error("First visible frame timed out, hidden stage=" + std::to_string(hidden_stage) +
        " active=" + std::to_string(NSApp.active) + " key=" + std::to_string(view.window.keyWindow) +
        " responder=" + std::string(view.window.firstResponder.description.UTF8String ?: "nil"));
    }
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
    if (variant == 4 || variant == 6) {
      if (host->GetFunc("ReaWeb_OpenEx")) hidden_tick();
      return;
    }
    if (!id) {
      if (!host->GetFunc("ReaWeb_Open")) return;
      if (variant == 3) {
        std::string url; std::ifstream(resource / "startup-test.url") >> url;
        id = reinterpret_cast<int(*)(const char*)>(host->GetFunc("ReaWeb_OpenDev"))(url.c_str());
      } else if (variant == 5) {
        id = reinterpret_cast<int(*)(const char*, const char*)>(host->GetFunc("ReaWeb_OpenEx"))(
          (resource / "Scripts/startup-test/index.html").c_str(), "{\"instanceKey\":\"startup-test.lua\",\"frameless\":true}");
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
    << "}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>"
    << (variant == 4 || variant == 6 ? "<script>reaper.lifecycle.ready.then(() => reaper.host.send('hidden-ready'))</script>" : "");
  started = GetTickCount(); host->Register("timer", reinterpret_cast<void*>(tick)); return 1;
}
