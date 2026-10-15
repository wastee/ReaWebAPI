// Opt-in desktop-composite acceptance inside a dedicated REAPER profile.
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
NSWindow* background;
int id = 0, phase = 0, variant = 0, frames = 0;
DWORD started = 0, due = 0;
NSColor* previous_half;
NSColor* previous_clear;
::id mouse_monitor;
bool received_clear_click = false;
NSPoint saved_pointer;
void tick();
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void finish(const std::string& text) {
  std::ofstream(resource / "transparent-test.log", std::ios::app) << text << '\n';
  host->Register("-timer", reinterpret_cast<void*>(tick));
  if (mouse_monitor) { [NSEvent removeMonitor:mouse_monitor]; mouse_monitor = nil; }
  if (background) [background close];
  PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
}
WKWebView* find_view() {
  for (NSWindow* window in NSApp.windows) {
    std::vector<NSView*> views{window.contentView};
    while (!views.empty()) {
      auto view = views.back(); views.pop_back();
      if ([view isKindOfClass:WKWebView.class]) return (WKWebView*)view;
      for (NSView* child in view.subviews) views.push_back(child);
    }
  }
  return nil;
}
void tick() {
  try {
    check(GetTickCount() - started < 30000, "Timed out");
    if (NSApp.modalWindow) {
      std::vector<NSView*> views{NSApp.modalWindow.contentView};
      while (!views.empty()) {
        auto view = views.back(); views.pop_back();
        if ([view isKindOfClass:NSButton.class] && ([[(NSButton*)view title] containsString:@"Still Evaluating"] || [[(NSButton*)view title] isEqualToString:@"No"])) [(NSButton*)view performClick:nil];
        for (NSView* child in view.subviews) views.push_back(child);
      }
      return;
    }
    if (!id) {
      if (!host->GetFunc("ReaWeb_OpenEx") || !IsWindowVisible(host->hwnd_main)) return;
      const auto path = (resource / "Scripts/Alpha/index.html").string();
      const auto options = std::string("{\"hidden\":true,\"transparent\":true,\"frameless\":") + (variant ? "false}" : "true}");
      id = reinterpret_cast<int(*)(const char*, const char*)>(host->GetFunc("ReaWeb_OpenEx"))(path.c_str(), options.c_str());
      if (!id) throw std::runtime_error(reinterpret_cast<const char*(*)()>(host->GetFunc("ReaWeb_GetLastError"))());
      std::ofstream(resource / "transparent-test.log", std::ios::app) << "opened=" << id << '\n';
    }
    auto view = find_view();
    if (!view) {
      if (++frames % 30 == 0) std::ofstream(resource / "transparent-test.log", std::ios::app) << "waiting for native view, open="
        << reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsOpen"))(id) << '\n';
      return;
    }
    if (phase == 0) {
      check(!view.window.visible, "Hidden window became visible");
      check(!view.window.opaque, "Native host is opaque");
      if (!reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsReady"))(id)) {
        if (++frames % 30 == 0) std::ofstream(resource / "transparent-test.log", std::ios::app) << "waiting for bridge, loading=" << view.loading << '\n';
        return;
      }
      [view.window setFrame:NSMakeRect(150, 150, 600, 420) display:NO];
      background = [[NSWindow alloc] initWithContentRect:NSMakeRect(50, 50, 1000, 750) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
      background.releasedWhenClosed = NO;
      background.level = NSFloatingWindowLevel;
      view.window.level = NSFloatingWindowLevel;
      background.backgroundColor = [NSColor colorWithSRGBRed:20/255.0 green:80/255.0 blue:180/255.0 alpha:1];
      [background orderFront:nil];
      check(reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Show"))(id), "Show failed");
      [view.window orderFront:nil];
      [background orderWindow:NSWindowBelow relativeTo:view.window.windowNumber];
      phase = 1; due = GetTickCount() + 800; return;
    }
    if (GetTickCount() < due) return;
    if (phase == 1 || phase == 2) {
      [background orderWindow:NSWindowBelow relativeTo:view.window.windowNumber];
      using Capture = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
      auto capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
      check(capture != nullptr, "Desktop capture unavailable");
      const auto frame = view.window.frame;
      const auto screen_height = NSScreen.screens.firstObject.frame.size.height;
      auto image = capture(CGRectMake(frame.origin.x-40, screen_height-NSMaxY(frame)-40, frame.size.width+80, frame.size.height+80),
        kCGWindowListOptionOnScreenOnly, kCGNullWindowID, kCGWindowImageDefault);
      check(image != nullptr, "Desktop capture denied");
      auto bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image]; CGImageRelease(image);
      const auto rect = [view convertRect:view.bounds toView:nil];
      const double scale = double(bitmap.pixelsWide)/(frame.size.width+80);
      const int left = (rect.origin.x+40)*scale, top = (frame.size.height-NSMaxY(rect)+40)*scale;
      auto clear = [[bitmap colorAtX:left+40*scale y:top+40*scale] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      auto half = [[bitmap colorAtX:left+160*scale y:top+160*scale] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      auto expected = [[bitmap colorAtX:5*scale y:5*scale] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      std::ofstream(resource / "transparent-test.log", std::ios::app) << "clear=" << clear.redComponent << ',' << clear.greenComponent << ',' << clear.blueComponent
        << " expected=" << expected.redComponent << ',' << expected.greenComponent << ',' << expected.blueComponent
        << " half=" << half.redComponent << ',' << half.greenComponent << ',' << half.blueComponent << '\n';
      [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:[NSString stringWithUTF8String:(resource / ("desktop-" + std::to_string(phase) + ".png")).c_str()] atomically:YES];
      check(std::abs(clear.redComponent-expected.redComponent)<.025 && std::abs(clear.greenComponent-expected.greenComponent)<.025 && std::abs(clear.blueComponent-expected.blueComponent)<.025,
        "Clear pixels did not reveal the native background");
      if (phase == 1) {
        previous_half = half;
        previous_clear = clear;
        background.backgroundColor = [NSColor colorWithSRGBRed:80/255.0 green:160/255.0 blue:40/255.0 alpha:1];
        [background display]; phase = 2; due = GetTickCount()+300;
      } else {
        check(std::abs(half.redComponent-previous_half.redComponent-(clear.redComponent-previous_clear.redComponent)/2)<.035 &&
          std::abs(half.greenComponent-previous_half.greenComponent-(clear.greenComponent-previous_clear.greenComponent)/2)<.035 &&
          std::abs(half.blueComponent-previous_half.blueComponent-(clear.blueComponent-previous_clear.blueComponent)/2)<.035, "Half Alpha did not blend with the changed background");
        check(!reinterpret_cast<bool(*)(int,bool)>(host->GetFunc("ReaWeb_SetDocked"))(id, true), "Dock was accepted");
        [view evaluateJavaScript:@"reaper.window.hide().then(()=>reaper.window.show()).then(()=>reaper.window.reload())" completionHandler:nil];
        phase = 3; due = GetTickCount()+800;
      }
    } else if (phase == 3) {
      check(view.window.visible && !view.window.opaque, "Hide/Show or Reload lost the Alpha host");
      check(CGPreflightPostEventAccess(), "Native input permission unavailable");
      saved_pointer = NSEvent.mouseLocation;
      __weak NSWindow* target = view.window;
      mouse_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^NSEvent*(NSEvent* event) {
        if (event.window == target) received_clear_click = true;
        return event;
      }];
      const auto point = [view.window convertPointToScreen:[view convertPoint:NSMakePoint(40, view.flipped ? 40 : view.bounds.size.height-40) toView:nil]];
      const auto global = CGPointMake(point.x, NSScreen.screens.firstObject.frame.size.height-point.y);
      for (const auto type : {kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp}) {
        auto event = CGEventCreateMouseEvent(nullptr, type, global, kCGMouseButtonLeft); CGEventPost(kCGHIDEventTap, event); CFRelease(event);
      }
      phase = 4; due = GetTickCount()+400;
    } else {
      CGWarpMouseCursorPosition(CGPointMake(saved_pointer.x, NSScreen.screens.firstObject.frame.size.height-saved_pointer.y));
      check(received_clear_click, "Transparent pixels passed mouse input through to the underlying window");
      finish("PASS: real REAPER desktop Alpha, half Alpha, rectangular input, hidden startup, Dock rejection and Hide/Show/Reload, framed=" + std::to_string(variant));
    }
  } catch (const std::exception& error) { finish("FAIL: phase=" + std::to_string(phase) + " " + error.what()); }
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "transparent-test.enabled")) return 0;
  std::ifstream(resource / "transparent-test.enabled") >> variant;
  const auto page = resource / "Scripts/Alpha/index.html"; fs::create_directories(page.parent_path());
  std::ofstream(page.parent_path() / "app.json") << R"({"id":"transparent-pixel-acceptance"})";
  std::ofstream(page) << R"(<!doctype html><style>html,body{background:transparent;margin:0}div{position:absolute;left:100px;top:100px;width:200px;height:160px;background:rgba(255,0,0,.5);border-radius:20px;box-shadow:0 8px 15px #0008}</style><div></div>)";
  started = GetTickCount(); host->Register("timer", reinterpret_cast<void*>(tick)); return 1;
}
