// Opt-in acceptance module for an isolated REAPER profile.
#include <reaper_plugin.h>
#include "platform/platform.hpp"
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <fstream>
#include <cmath>

namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
int (*open_window)(const char*, const char*, const char*, const bool*);
bool (*set_docked)(int, bool), (*is_docked)(int);
int (*dock_index)(HWND, bool*), (*dock_position)(int);
int id, phase, step, captures, expected_position;
HWND window;
WKWebView* view;
WKWebView* original_view;
bool pending, checked;
std::string failure;
DWORD started;
NSPoint divider_origin;
double minimum_size, maximum_size;
bool divider_held;
NSPoint saved_cursor;
bool cursor_saved;

void mouse_event(NSEventType type, NSPoint point) {
  const auto screen = [view.window convertPointToScreen:point];
  CGWarpMouseCursorPosition(CGPointMake(screen.x, NSScreen.screens.firstObject.frame.size.height - screen.y));
  auto event = [NSEvent mouseEventWithType:type location:point modifierFlags:0
    timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:view.window.windowNumber
    context:nil eventNumber:0 clickCount:1 pressure:type == NSEventTypeLeftMouseUp ? 0 : 1];
  [NSApp sendEvent:event];
}

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void log(const std::string& text) { std::ofstream(resource / "resize-test.log", std::ios::app) << text << '\n'; }
void tick();
void finish(const std::string& text) {
  if (divider_held) { mouse_event(NSEventTypeLeftMouseUp, divider_origin); divider_held = false; }
  if (cursor_saved) CGWarpMouseCursorPosition(CGPointMake(saved_cursor.x, NSScreen.screens.firstObject.frame.size.height - saved_cursor.y));
  log(text);
  host->Register("-timer", reinterpret_cast<void*>(tick));
  PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
}
WKWebView* webview(HWND handle) {
  std::vector<NSView*> views{(__bridge NSView*)GetDlgItem(handle, 0)};
  while (!views.empty()) {
    auto candidate = views.back(); views.pop_back();
    if ([candidate isKindOfClass:WKWebView.class]) return (WKWebView*)candidate;
    for (NSView* child in candidate.subviews) views.push_back(child);
  }
  return nil;
}
HWND find_window() {
  HWND found = nullptr;
  EnumWindows(+[](HWND handle, LPARAM result) -> BOOL {
    char name[512]{}; GetWindowText(handle, name, sizeof(name));
    if (std::string(name) == "Resize acceptance") *reinterpret_cast<HWND*>(result) = handle;
    return TRUE;
  }, reinterpret_cast<LPARAM>(&found));
  return found;
}
bool background(unsigned expected) {
  auto color = [view.underPageBackgroundColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (!color) return false;
  const auto rgb = (unsigned(std::lround(color.redComponent * 255)) << 16) |
    (unsigned(std::lround(color.greenComponent * 255)) << 8) | unsigned(std::lround(color.blueComponent * 255));
  return rgb == expected;
}
void evaluate(NSString* script) {
  pending = true; checked = false;
  [view evaluateJavaScript:script completionHandler:^(::id value, NSError* error) {
    if (error) failure = error.description.UTF8String;
    else if (![value respondsToSelector:@selector(boolValue)] || ![value boolValue]) failure = "Page state/viewport check failed";
    pending = false; checked = true;
  }];
}
void sample() {
  using Capture = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
  static auto capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
  require(capture != nullptr, "Native window capture API unavailable");
  auto native = view.window;
  auto image = capture(CGRectNull, kCGWindowListOptionIncludingWindow, CGWindowID(native.windowNumber), kCGWindowImageBoundsIgnoreFraming);
  require(image != nullptr, "Cannot capture the test window. Screen recording permission may be required");
  auto bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image]; CGImageRelease(image);
  const auto bounds = [view convertRect:view.visibleRect toView:nil];
  const double scale = double(bitmap.pixelsWide) / native.frame.size.width;
  const int left = int(std::ceil(bounds.origin.x * scale)), top = int(std::ceil((native.frame.size.height - NSMaxY(bounds)) * scale));
  const int width = int(std::floor(bounds.size.width * scale)), height = int(std::floor(bounds.size.height * scale));
  require(width > 40 && height > 40, "WebView visible area is too small to sample");
  auto marker = [[bitmap colorAtX:left + int(20 * scale) y:top + int(20 * scale)] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  bool content_visible = false;
  // The compositor may still present the preceding divider position for one frame.
  for (int y = top + 4; y < top + std::min(height, int(80 * scale)) && !content_visible; y += 4)
    for (int x = left + 4; x < left + std::min(width, int(80 * scale)); x += 4) {
      auto color = [[bitmap colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      if (color.greenComponent > .7 && color.greenComponent - color.redComponent > .3 &&
          color.greenComponent - color.blueComponent > .3) { content_visible = true; break; }
    }
  int whites = 0, samples = 0;
  for (int row = 0; row < 9; ++row) for (int col = 0; col < 17; ++col) {
    const int x = left + 4 + (width - 9) * col / 16, y = top + 4 + (height - 9) * row / 8;
    if (x < 0 || y < 0 || x >= bitmap.pixelsWide || y >= bitmap.pixelsHigh) continue;
    auto color = [[bitmap colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (color.redComponent > .94 && color.greenComponent > .94 && color.blueComponent > .94) ++whites;
    ++samples;
  }
  require(samples == 153, "Native capture does not cover the WebView client area");
  if (whites || !content_visible || step == 1 || step == 60) {
    log(std::string("marker=") + marker.description.UTF8String + " rect=" + std::to_string(left) + "," + std::to_string(top));
    log(std::string("visible=") + NSStringFromRect(view.visibleRect).UTF8String + " bounds=" + NSStringFromRect(view.bounds).UTF8String);
    if (whites) for (NSView* parent = view; parent; parent = parent.superview) {
      log(std::string(parent.className.UTF8String) + " frame=" + NSStringFromRect(parent.frame).UTF8String +
        " visible=" + NSStringFromRect(parent.visibleRect).UTF8String + " layer=" + (parent.layer ? "yes" : "no"));
    }
    const auto path = resource / ("resize-" + std::to_string(phase) + "-" + std::to_string(step) + ".png");
    [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
      writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
  }
  log("sample phase=" + std::to_string(phase) + " step=" + std::to_string(step) + " white=" + std::to_string(whites));
  require(whites == 0, "White pixels appeared during native resizing");
  require(content_visible, "Page content disappeared during native resizing");
  ++captures;
}
void advance() { log("phase=" + std::to_string(phase++) + " passed"); step = 0; checked = false; started = GetTickCount(); }
bool resize() {
  if (pending) return false;
  if (!step) checked = false;
  require(webview(window) == original_view, "Resizing/docking recreated the WebView");
  if (step > 0) sample();
  if (step == 60) {
    if (!checked) {
      const auto bounds = view.bounds;
      evaluate([NSString stringWithFormat:@"window.resizeToken==='retained' && window.animationFrames>10 && document.querySelector('#state').value==='preserved' && Math.abs(innerWidth-%.0f)<=1 && Math.abs(innerHeight-%.0f)<=1",
        bounds.size.width, bounds.size.height]);
      return false;
    }
    return true;
  }
  // Direct native resizing covers viewport updates separately from host divider dragging.
  RECT rect{}; GetWindowRect(window, &rect);
  const int width = 380 + (step % 12) * 31, height = 180 + (step % 10) * 19;
  SetWindowPos(window, nullptr, rect.left, rect.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  [view.window displayIfNeeded];
  ++step;
  return false;
}
bool resize_divider() {
  if (pending) return false;
  require(webview(window) == original_view, "Divider dragging recreated the WebView");
  if (!step) {
    if (!cursor_saved) { saved_cursor = NSEvent.mouseLocation; cursor_saved = true; }
    [NSApp activateIgnoringOtherApps:YES]; [view.window makeKeyAndOrderFront:nil];
    auto container = (__bridge NSView*)GetParent(window);
    require([container isKindOfClass:NSView.class], "Docker container is not an NSView");
    const auto rect = [container convertRect:container.bounds toView:nil];
    divider_origin = NSMakePoint(NSMidX(rect), NSMidY(rect));
    if (expected_position == 0) divider_origin.y = NSMaxY(rect) - 2;
    if (expected_position == 2) divider_origin.y = NSMinY(rect) + 2;
    if (expected_position == 1) divider_origin.x = NSMaxX(rect) - 2;
    if (expected_position == 3) divider_origin.x = NSMinX(rect) + 2;
    log(std::string("divider container=") + NSStringFromRect(rect).UTF8String + " origin=" + NSStringFromPoint(divider_origin).UTF8String);
    auto hit = [view.window.contentView hitTest:[view.window.contentView convertPoint:divider_origin fromView:nil]];
    log(std::string("divider hit=") + hit.className.UTF8String);
    minimum_size = maximum_size = expected_position % 2 ? view.bounds.size.width : view.bounds.size.height;
    mouse_event(NSEventTypeLeftMouseDown, divider_origin); divider_held = true;
    log("divider captured=" + std::to_string(reinterpret_cast<uintptr_t>(GetCapture())));
  } else {
    sample();
    const auto size = expected_position % 2 ? view.bounds.size.width : view.bounds.size.height;
    minimum_size = std::min(minimum_size, size); maximum_size = std::max(maximum_size, size);
  }
  if (step == 90) {
    if (divider_held) {
      mouse_event(NSEventTypeLeftMouseUp, divider_origin); divider_held = false;
      log("divider size range=" + std::to_string(minimum_size) + ".." + std::to_string(maximum_size));
      require(maximum_size - minimum_size > 100, "Mouse events did not resize the REAPER Docker divider");
    }
    if (!checked) {
      evaluate([NSString stringWithFormat:@"window.resizeToken==='retained' && window.animationFrames>10 && document.querySelector('#state').value==='preserved' && Math.abs(innerWidth-%.0f)<=1 && Math.abs(innerHeight-%.0f)<=1",
        view.bounds.size.width, view.bounds.size.height]);
      return false;
    }
    return true;
  }
  auto point = divider_origin;
  const double delta = 75 * std::sin((step + 1) * 6.283185307179586 / 30);
  if (expected_position % 2) point.x += delta; else point.y += delta;
  mouse_event(NSEventTypeLeftMouseDragged, point);
  ++step;
  return false;
}
void tick() {
  try {
    require(failure.empty(), failure.c_str());
    require(GetTickCount() - started < 45000, "Resize acceptance timed out");
    if (NSApp.modalWindow) {
      std::vector<NSView*> views{NSApp.modalWindow.contentView};
      while (!views.empty()) {
        auto candidate = views.back(); views.pop_back();
        if ([candidate isKindOfClass:NSButton.class] && [[(NSButton*)candidate title] isEqualToString:@"No"])
          [(NSButton*)candidate performClick:nil];
        for (NSView* child in candidate.subviews) views.push_back(child);
      }
      return;
    }
    if (!id) {
      if (!IsWindowVisible(host->hwnd_main)) return;
      open_window = reinterpret_cast<decltype(open_window)>(host->GetFunc("ReaWeb_Open"));
      if (!open_window) return;
      set_docked = reinterpret_cast<decltype(set_docked)>(host->GetFunc("ReaWeb_SetDocked"));
      is_docked = reinterpret_cast<decltype(is_docked)>(host->GetFunc("ReaWeb_IsDocked"));
      dock_index = reinterpret_cast<decltype(dock_index)>(host->GetFunc("DockIsChildOfDock"));
      dock_position = reinterpret_cast<decltype(dock_position)>(host->GetFunc("DockGetPosition"));
      id = open_window((resource / "Scripts/resize-test/index.html").c_str(), "resize-acceptance", nullptr, nullptr);
      require(id != 0, "Cannot open resize page"); return;
    }
    if (!window) {
      window = find_window(); if (!window) return;
      view = webview(window); original_view = view;
      require(view != nil, "No WKWebView in native host");
    }
    if (pending) return;
    switch (phase) {
      case 0:
        if (!background(0x182838)) return;
        require(view.superview.wantsLayer, "Native WebView container is not layer backed");
        evaluate(@"window.resizeToken==='retained'"); advance(); break;
      case 1:
        if (resize()) advance(); break;
      case 2:
        require(set_docked(id, true), "Dock failed"); advance(); break;
      case 3: {
        bool floating = false;
        const int index = dock_index(window, &floating);
        require(index >= 0 && is_docked(id), "WebView is not docked");
        require(dock_position(index) == expected_position && floating == (expected_position == 4),
          "Docker is not in the requested screen position");
        if (expected_position < 4 ? resize_divider() : resize()) advance(); break;
      }
      case 4:
        require(!set_docked(id, false), "Undock failed"); advance(); break;
      case 5:
        if (resize()) advance(); break;
      case 6:
        evaluate(@"document.documentElement.style.background='#e8d8c8'; true"); advance(); break;
      case 7:
        if (background(0xe8d8c8)) advance(); break;
      case 8:
        evaluate(@"document.documentElement.style.background='rgba(0,0,0,.5)'; true"); advance(); break;
      case 9:
        if (!background(0xffffff)) return;
        require(captures >= 180, "Not enough native resize frame samples");
        finish("PASS: native resize pixels, viewport/state, docking, undocking, theme and translucent fallback; captures=" + std::to_string(captures));
        break;
    }
  } catch (const std::exception& error) { finish(std::string("FAIL: ") + error.what()); }
}
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) { view = original_view = nil; return 0; }
  host = rec;
  resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "resize-test.enabled")) return 0;
  std::ifstream(resource / "resize-test.enabled") >> expected_position;
  const auto page = resource / "Scripts/resize-test/index.html"; fs::create_directories(page.parent_path());
  std::ofstream(page) << R"(<!doctype html><title>Resize acceptance</title>
<style>html{background:#182838}body{margin:0;overflow:hidden}#state{display:none}
#marker{position:fixed;left:8px;top:8px;width:24px;height:24px;background:#00ff00}</style>
<div id="marker"></div><input id="state" value="preserved"><script>window.resizeToken='retained';window.animationFrames=0;
function animate(){++window.animationFrames;requestAnimationFrame(animate)}requestAnimationFrame(animate);</script>)";
  log("macOS native resize acceptance");
  started = GetTickCount(); host->Register("timer", reinterpret_cast<void*>(tick));
  return 1;
}
