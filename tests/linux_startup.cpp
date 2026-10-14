// Opt-in startup fixture for independent X11 frame sampling.
#include <reaper_plugin.h>
#include "platform/platform.hpp"
#include <fstream>
namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
int id = 0, variant = 0;
DWORD started;
bool evaluating = false;
void tick() {
  evaluating = false;
  EnumWindows(+[](HWND dialog, LPARAM) -> BOOL {
    if (dialog != host->hwnd_main) EnumChildWindows(dialog, +[](HWND child, LPARAM) -> BOOL {
      char text[128]{}; GetWindowText(child, text, sizeof(text));
      if (std::string(text) == "Still Evaluating" || std::string(text).rfind("Buy Me", 0) == 0) evaluating = true;
      if (std::string(text) == "Still Evaluating" && IsWindowEnabled(child))
        SendMessage(GetParent(child), WM_COMMAND, MAKELONG(GetWindowLong(child, GWL_ID), BN_CLICKED), reinterpret_cast<LPARAM>(child));
      return TRUE;
    }, 0);
    return TRUE;
  }, 0);
  if (evaluating || GetTickCount() - started < 1000) return;
  if (!id && IsWindowVisible(host->hwnd_main) && host->GetFunc("ReaWeb_Open")) {
    if (variant == 3) {
      std::string url; std::ifstream(resource / "startup-test.url") >> url;
      id = reinterpret_cast<int(*)(const char*)>(host->GetFunc("ReaWeb_OpenDev"))(url.c_str());
    } else id = reinterpret_cast<int(*)(const char*, const char*, const char*, const bool*)>(host->GetFunc("ReaWeb_Open"))(
      (resource / "Scripts/startup-test/index.html").c_str(), "startup-test.lua", nullptr, nullptr);
    std::ofstream(resource / "startup-test.log", std::ios::app) << "opened=" << id << '\n';
    if (!id) {
      std::ofstream(resource / "startup-test.log", std::ios::app) << reinterpret_cast<const char*(*)()>(host->GetFunc("ReaWeb_GetLastError"))();
      id = -1;
    }
  }
  if (fs::exists(resource / "close")) {
    host->Register("-timer", reinterpret_cast<void*>(tick)); PostMessage(host->hwnd_main, WM_CLOSE, 0, 0);
  }
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec; resource = fs::u8path(reinterpret_cast<const char*(*)()>(host->GetFunc("GetResourcePath"))());
  if (!fs::exists(resource / "startup-test.enabled")) return 0;
  std::ifstream(resource / "startup-test.enabled") >> variant;
  const auto page = resource / "Scripts/startup-test/index.html"; fs::create_directories(page.parent_path());
  const auto background = variant == 1 ? "#e8d8c8" : variant == 2 ? "linear-gradient(120deg,#182838,#486878)" : "#182838";
  std::ofstream(page) << "<!doctype html><title>Startup acceptance</title><style>html{min-height:100%;background:" << background
    << "}body{margin:0}#marker{width:100px;height:100px;background:#00ff00}</style><div id=marker></div>";
  started = GetTickCount(); host->Register("timer", reinterpret_cast<void*>(tick)); return 1;
}
