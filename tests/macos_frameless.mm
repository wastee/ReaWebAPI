// Opt-in native input acceptance in an isolated REAPER profile.
#include <reaper_plugin.h>
#include "platform/platform.hpp"
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ApplicationServices/ApplicationServices.h>
#include <atomic>
#include <fstream>
#include <thread>

namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
int window_id = 0, phase = 0, edge = 0, dock_cycle = 0;
bool resizable = true, pending = false, checked = false, input_enabled = true;
bool input_requested = false;
bool appearance_checked = false;
WKWebView* view;
WKWebView* original;
NSRect before;
NSPoint saved_cursor;
DWORD started, due;
std::string failure;
std::thread input;
std::atomic<bool> input_done{true};
const int edges[][2] = {{0,1},{2,1},{1,0},{1,2},{0,0},{2,0},{0,2},{2,2}};
void tick();
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void log(const std::string& message) { std::ofstream(resource / "frameless-test.log", std::ios::app) << message << '\n'; }
WKWebView* find_view() {
  for (NSWindow* native in NSApp.windows) {
    std::vector<NSView*> queue{native.contentView};
    while (!queue.empty()) {
      auto candidate = queue.back(); queue.pop_back();
      if ([candidate isKindOfClass:WKWebView.class]) return (WKWebView*)candidate;
      for (NSView* child in candidate.subviews) queue.push_back(child);
    }
  }
  return nil;
}
void evaluate(NSString* script) {
  pending = true; checked = false;
  [view evaluateJavaScript:script completionHandler:^(::id value, NSError* error) {
    if (error) failure = error.description.UTF8String;
    else if (![value respondsToSelector:@selector(boolValue)] || ![value boolValue]) failure = "Page state check failed";
    pending = false; checked = true;
  }];
}
void evaluate_async(NSString* script) {
  pending = true; checked = false;
  [view callAsyncJavaScript:script arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(::id value, NSError* error) {
    if (error) failure = error.description.UTF8String;
    else if (![value respondsToSelector:@selector(boolValue)] || ![value boolValue]) failure = "Appearance state check failed";
    pending = false; checked = true;
  }];
}
void gesture(double x, double y, double dx, double dy) {
  require(CGPreflightPostEventAccess(), "Native mouse input permission unavailable");
  if (input.joinable()) input.join();
  input_done = false;
  input = std::thread([=] {
    const auto post = [](CGEventType type, double px, double py) {
      auto event = CGEventCreateMouseEvent(nullptr, type, CGPointMake(px, py), kCGMouseButtonLeft);
      CGEventPost(kCGHIDEventTap, event); CFRelease(event);
    };
    post(kCGEventMouseMoved, x, y); std::this_thread::sleep_for(std::chrono::milliseconds(120));
    post(kCGEventLeftMouseDown, x, y); std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (dx || dy) for (int n = 1; n <= 10; ++n) {
      post(kCGEventLeftMouseDragged, x + dx*n/10, y + dy*n/10);
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    post(kCGEventLeftMouseUp, x + dx, y + dy);
    std::this_thread::sleep_for(std::chrono::milliseconds(250)); input_done = true;
  });
}
void finish(const std::string& message) {
  if (input.joinable()) input.join();
  CGWarpMouseCursorPosition(CGPointMake(saved_cursor.x, NSScreen.screens.firstObject.frame.size.height - saved_cursor.y));
  log(message); host->Register("-timer", reinterpret_cast<void*>(tick));
  PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
}
void tick() {
  try {
    require(GetTickCount() - started < 90000, "Native frameless acceptance timed out");
    if (!failure.empty()) throw std::runtime_error(failure);
    if (pending || !input_done || GetTickCount() < due) return;
    if (NSApp.modalWindow) {
      std::vector<NSView*> queue{NSApp.modalWindow.contentView};
      while (!queue.empty()) {
        auto candidate = queue.back(); queue.pop_back();
        if ([candidate isKindOfClass:NSButton.class] && [[(NSButton*)candidate title] isEqualToString:@"No"])
          [(NSButton*)candidate performClick:nil];
        for (NSView* child in candidate.subviews) queue.push_back(child);
      }
      return;
    }
    if (!IsWindowVisible(host->hwnd_main)) return;
    const auto open = reinterpret_cast<int(*)(const char*, const char*)>(host->GetFunc("ReaWeb_OpenEx"));
    if (!open) return;
    if (!window_id) {
      window_id = open((resource / "Scripts/Frameless/index.html").c_str(),
        resizable ? "{\"hidden\":true,\"frameless\":true}" : "{\"hidden\":true,\"frameless\":true,\"resizable\":false}");
      if (!window_id) throw std::runtime_error(reinterpret_cast<const char*(*)()>(host->GetFunc("ReaWeb_GetLastError"))());
      log("opened=" + std::to_string(window_id)); due = GetTickCount() + 1500; return;
    }
    if (phase == 0) {
      view = find_view(); if (!view || !reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsReady"))(window_id)) return;
      original = view;
      require(!view.window.visible, "Hidden frameless window became visible");
      require(!(view.window.styleMask & NSWindowStyleMaskTitled), "Native title bar remains");
      require(bool(view.window.styleMask & NSWindowStyleMaskResizable) == resizable, "Incorrect resize style");
      reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Show"))(window_id);
      reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Focus"))(window_id);
      [view.window setFrame:NSMakeRect(200, 200, 600, 400) display:YES];
      phase = 1; due = GetTickCount() + 1000; return;
    }
    if (phase < 13) require(find_view() == original, "The WebView was recreated");
    const double screen_height = NSScreen.screens.firstObject.frame.size.height;
    if (phase == 1) {
      if (!view.window.keyWindow) {
        reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Focus"))(window_id);
        due = GetTickCount()+250; return;
      }
      log(std::string("native=") + view.window.className.UTF8String + " canKey=" + std::to_string(view.window.canBecomeKeyWindow) +
        " visible=" + std::to_string(view.window.visible) + " active=" + std::to_string(NSApp.active) +
        " key=" + (NSApp.keyWindow ? NSApp.keyWindow.title.UTF8String : "none") +
        " accessibility=" + std::to_string(AXIsProcessTrusted()) + " postEvents=" + std::to_string(CGPreflightPostEventAccess()));
      require(view.window.keyWindow, "Frameless window did not accept keyboard focus");
      if (!input_enabled) {
        evaluate(@"document.querySelector('button').click();window.marker===731 && window.clicks===1");
        phase = 8; return;
      }
      if (!CGPreflightPostEventAccess()) {
        if (!input_requested) { input_requested = true; CGRequestPostEventAccess(); log("Waiting for native mouse input authorization"); }
        due = GetTickCount()+1000; return;
      }
      before = view.window.frame;
      gesture(before.origin.x + 150, screen_height - NSMaxY(before) + 28, 60, 35); phase = 2;
    } else if (phase == 2) {
      const auto frame = view.window.frame;
      log(std::string("drag ") + NSStringFromRect(before).UTF8String + " -> " + NSStringFromRect(frame).UTF8String);
      require(std::abs(frame.origin.x-before.origin.x-60) < 2 && std::abs(frame.origin.y-before.origin.y+35) < 2, "Native title drag did not move the window");
      phase = 3;
    } else if (phase == 3) {
      [view.window setFrame:NSMakeRect(200, 200, 600, 400) display:YES]; phase = 4; due = GetTickCount() + 250;
    } else if (phase == 4) {
      before = view.window.frame;
      const auto h = edges[edge][0], v = edges[edge][1];
      const auto x = h == 0 ? NSMinX(before)+2 : h == 2 ? NSMaxX(before)-2 : NSMidX(before);
      const auto y = v == 0 ? screen_height-NSMaxY(before)+2 : v == 2 ? screen_height-NSMinY(before)-2 : screen_height-NSMidY(before);
      gesture(x, y, h == 1 ? 0 : 30, v == 1 ? 0 : 30); phase = 5;
    } else if (phase == 5) {
      const auto frame = view.window.frame;
      log("edge=" + std::to_string(edge) + " frame=" + NSStringFromRect(frame).UTF8String);
      require((std::abs(frame.size.width-before.size.width) > 10 || std::abs(frame.size.height-before.size.height) > 10) == resizable, "Unexpected native edge resize result");
      phase = ++edge < 8 ? 3 : 6;
    } else if (phase == 6) {
      before = view.window.frame;
      gesture(NSMaxX(before)-45, screen_height-NSMaxY(before)+26, 0, 0); phase = 7;
    } else if (phase == 7) {
      require(NSEqualRects(view.window.frame, before), "Button click moved the window");
      evaluate(@"window.marker===731 && window.clicks===1"); phase = 8;
    } else if (phase == 8) {
      require(checked, "Button check incomplete");
      if (!appearance_checked) {
        appearance_checked = true;
        auto menu = [NSMenu new]; [menu addItemWithTitle:@"Copy" action:nil keyEquivalent:@""];
        [view willOpenMenu:menu withEvent:nil];
        require(menu.numberOfItems == 6, "Page window menu does not match the native menu");
        require([[menu itemAtIndex:1].title isEqualToString:@"Reload"] && [[menu itemAtIndex:2].title isEqualToString:@"Open DevTools"], "Page menu actions differ");
        require([[menu itemAtIndex:4].title hasSuffix:@" Folder"] && [[menu itemAtIndex:5].title hasPrefix:@"Close "], "Page menu lacks folder/close");
        evaluate_async(@"const w=reaper.window; const initial=await w.getPosition(), size=await w.getSize();"
          "for(let i=0;i<3;++i){await w.setFrameless(false);if((await w.getState()).framelessActive)throw Error('framed state');"
          "await w.setFrameless(true);const p=await w.getPosition(),s=await w.getSize();"
          "if(p.x!==initial.x||p.y!==initial.y||s.width!==size.width||s.height!==size.height)throw Error('bounds changed');}"
          "await w.setCornerRadius(24);if(!(await w.getState()).cornerRadiusActive)throw Error('rounding inactive');"
          "await w.setFrameless(false);if((await w.getState()).cornerRadiusActive)throw Error('framed custom clipping');"
          "await w.setFrameless(true);await w.hide();await w.setFrameless(false);await w.setFrameless(true);"
          "if((await w.getState()).visible)throw Error('hidden window shown');await w.show();await w.focus();"
          "return window.marker===731&&window.clicks===1;");
        return;
      }
      require(view.superview.layer.cornerRadius == 24 || dock_cycle > 0, "Native corner radius was not applied");
      require(reinterpret_cast<bool(*)(int,bool)>(host->GetFunc("ReaWeb_SetDocked"))(window_id, true), "Dock failed");
      phase = 9; due = GetTickCount()+300;
    } else if (phase == 9) {
      require(view.window.styleMask & NSWindowStyleMaskTitled, "Shared Docker decoration was removed");
      require(view.superview.layer.cornerRadius == 0, "Docker retained custom corner clipping");
      evaluate_async(@"await reaper.window.setFrameless(false);await reaper.window.setCornerRadius(32);"
        "await reaper.window.setFrameless(true);const s=await reaper.window.getState();"
        "return s.docked&&!s.framelessActive&&!s.cornerRadiusActive&&window.marker===731&&window.clicks===1;"); phase = 10;
    } else if (phase == 10) {
      require(checked, "Dock page check incomplete");
      reinterpret_cast<bool(*)(int,bool)>(host->GetFunc("ReaWeb_SetDocked"))(window_id, false);
      phase = 11; due = GetTickCount()+300;
    } else if (phase == 11) {
      require(!(view.window.styleMask & NSWindowStyleMaskTitled), "Undock did not restore frameless style");
      require(bool(view.window.styleMask & NSWindowStyleMaskResizable) == resizable, "Undock lost resize configuration");
      require(view.superview.layer.cornerRadius == 32, "Undock did not restore custom corners");
      evaluate(@"window.marker===731 && window.clicks===1"); phase = ++dock_cycle < 3 ? 8 : 12;
    } else if (phase == 12) {
      [view.window setFrame:NSMakeRect(200, 200, 432, 321) display:YES];
      require(view.window.frame.size.width == 432, "Programmatic fixed window sizing failed");
      require(reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_Close"))(window_id), "Close failed");
      phase = 13; due = GetTickCount()+300;
    } else if (phase == 13) {
      require(!reinterpret_cast<bool(*)(int)>(host->GetFunc("ReaWeb_IsOpen"))(window_id), "Closed window remained open");
      finish(std::string(input_enabled ?
        "PASS: hidden frameless, native drag, eight resize edges, button exclusion, repeated Dock/Undock, retained WebView and close, resizable=" :
        "PASS: hidden frameless, focus, native styles, repeated Dock/Undock, retained WebView, programmatic sizing and close (no mouse injection), resizable=") + std::to_string(resizable));
    }
  } catch (const std::exception& error) { finish(std::string("FAIL: phase=") + std::to_string(phase) + " " + error.what()); }
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "frameless-test.enabled")) return 0;
  std::ifstream(resource / "frameless-test.enabled") >> resizable;
  input_enabled = !fs::exists(resource / "frameless-test.no-input");
  auto page = resource / "Scripts/Frameless/index.html"; fs::create_directories(page.parent_path());
  std::ofstream(page.parent_path() / "app.json") << R"({"id":"frameless-acceptance"})";
  std::ofstream(page) << R"HTML(<!doctype html><title>Frameless acceptance</title><style>html{background:#182838;color:white}body{margin:0}header{height:60px;padding:16px;box-sizing:border-box}button{float:right}</style><header data-reaweb-drag>Native title <button onclick="++window.clicks">Button</button></header><input value="Editable"><script>window.marker=731;window.clicks=0;</script>)HTML";
  saved_cursor = NSEvent.mouseLocation; started = GetTickCount();
  host->Register("timer", reinterpret_cast<void*>(tick)); return 1;
}
