// Opt-in probe for real REAPER divider input and external X11 frame capture.
#include <reaper_plugin.h>
#include "platform/platform.hpp"
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <fstream>

namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
HWND window;
int id, phase;
DWORD started;
int (*open_window)(const char*, const char*, const char*, const bool*);
bool (*set_docked)(int, bool), (*send)(int, const char*);
const char* (*receive)(int);

Json rectangle(HWND handle) {
  RECT rect{}; GetWindowRect(handle, &rect);
  return {rect.left, rect.top, rect.right, rect.bottom};
}
void tick() {
  EnumWindows(+[](HWND dialog, LPARAM) -> BOOL {
    if (dialog != host->hwnd_main) EnumChildWindows(dialog, +[](HWND child, LPARAM) -> BOOL {
      char text[128]{}; GetWindowText(child, text, sizeof(text));
      if (std::string(text) == "Still Evaluating" && IsWindowEnabled(child))
        SendMessage(GetParent(child), WM_COMMAND, MAKELONG(GetWindowLong(child, GWL_ID), BN_CLICKED), reinterpret_cast<LPARAM>(child));
      return TRUE;
    }, 0);
    return TRUE;
  }, 0);
  if (!id) {
    if (!IsWindowVisible(host->hwnd_main)) return;
    open_window = reinterpret_cast<decltype(open_window)>(host->GetFunc("ReaWeb_Open"));
    if (!open_window) return;
    set_docked = reinterpret_cast<decltype(set_docked)>(host->GetFunc("ReaWeb_SetDocked"));
    send = reinterpret_cast<decltype(send)>(host->GetFunc("ReaWeb_Send"));
    receive = reinterpret_cast<decltype(receive)>(host->GetFunc("ReaWeb_Receive"));
    SetWindowPos(host->hwnd_main, nullptr, 40, 40, 1200, 860, SWP_NOZORDER);
    id = open_window((resource / "Scripts/resize-test/index.html").c_str(), "resize-acceptance", nullptr, nullptr);
    std::ofstream(resource / "resize-test.log", std::ios::app) << "opened=" << id << '\n';
  }
  if (!window) {
    EnumWindows(+[](HWND handle, LPARAM) -> BOOL {
      char title[128]{}; GetWindowText(handle, title, sizeof(title));
      if (std::string(title) == "Resize acceptance") window = handle;
      return TRUE;
    }, 0);
    if (!window) return;
  }
  const std::string message = receive(id);
  if (!message.empty()) std::ofstream(resource / "resize-test.log", std::ios::app) << "message=" << message << '\n';
  if (phase == 0 && message == "READY") {
    set_docked(id, true); phase = 1; started = GetTickCount();
  }
  if (phase == 1 && GetTickCount() - started > 1200) phase = 2;
  if (phase == 2) {
    auto native = SWELL_GetOSWindow(host->hwnd_main, "GdkWindow");
    auto xid_of = reinterpret_cast<::Window(*)(void*)>(dlsym(RTLD_DEFAULT, "gdk_x11_window_get_xid"));
    bool floating = false;
    const auto index = reinterpret_cast<int(*)(HWND, bool*)>(host->GetFunc("DockIsChildOfDock"))(window, &floating);
    const auto position = reinterpret_cast<int(*)(int)>(host->GetFunc("DockGetPosition"))(index);
    Json state{{"page", rectangle(window)}, {"docker", rectangle(GetParent(window))},
      {"main", rectangle(host->hwnd_main)}, {"xid", xid_of(native)}, {"position", position}, {"floating", floating}};
    { std::ofstream out(resource / "geometry.tmp"); out << state; }
    fs::rename(resource / "geometry.tmp", resource / "geometry.json");
    if (fs::exists(resource / "verify")) { fs::remove(resource / "verify"); send(id, "verify"); }
    if (!message.empty()) std::ofstream(resource / "state.json") << message;
  }
  if (fs::exists(resource / "close")) {
    host->Register("-timer", reinterpret_cast<void*>(tick));
    PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
  }
}
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "resize-test.enabled")) return 0;
  const auto page = resource / "Scripts/resize-test/index.html"; fs::create_directories(page.parent_path());
  std::ofstream(page) << R"(<!doctype html><title>Resize acceptance</title>
<style>html{background:#182838}body{margin:0;overflow:hidden}#state{display:none}
#marker{position:fixed;left:8px;top:8px;width:24px;height:24px;background:#00ff00}</style>
<div id="marker"></div><input id="state" value="preserved"><script>let frames=0;
function animate(){++frames;requestAnimationFrame(animate)}requestAnimationFrame(animate);
reaper.lifecycle.ready.then(async()=>{await reaper.events.on('message',()=>reaper.host.send(JSON.stringify({
value:document.querySelector('#state').value,frames,width:innerWidth,height:innerHeight,
marker:getComputedStyle(document.querySelector('#marker')).backgroundColor,markerRect:document.querySelector('#marker').getBoundingClientRect()})));
await reaper.host.send('READY')});</script>)";
  host->Register("timer", reinterpret_cast<void*>(tick));
  return 1;
}
