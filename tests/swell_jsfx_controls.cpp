#include "../src/platform/shared/jsfx_editor.cpp"
#include <swell/swell-dlggen.h>
#include <fstream>

namespace {
using namespace reaweb;
reaper_plugin_info_t* host;
fs::path resource;
HWND editor, title;
std::vector<HWND> content;
int hidden_bounds, meter_lookups;
JsfxDiscovery observer;
Host observer_host;
void* track;
bool match_editor;
double observer_deadline;
uint64_t preparations;
decltype(GetWindowRect) original_rect;
decltype(GetDlgItem) original_item;
bool visible(HWND window) { return (GetWindowLong(window, GWL_STYLE) & WS_VISIBLE) != 0; }
bool tracked_rect(HWND window, RECT* rect) {
  if (window != title && std::find(content.begin(), content.end(), window) != content.end() && !visible(window)) ++hidden_bounds;
  return original_rect(window, rect);
}
HWND tracked_item(HWND window, int id) {
  if (window == editor && (id == 1089 || id == 1091)) ++meter_lookups;
  return original_item(window, id);
}
void check(bool value) { if (!value) throw std::runtime_error("JSFX control concealment regression"); }
INT_PTR procedure(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
SWELL_DialogRegHelper dialog(&SWELL_curmodule_dialogresource_head, [](HWND, int) {}, 501,
  SWELL_DLG_WS_RESIZABLE | SWELL_DLG_WS_FLIPPED, "JSFX controls", 700, 520, 1.0, 1.0);
void finish(const std::string& result) {
  observer.reset();
  if (track) reinterpret_cast<void(*)(void*)>(host->GetFunc("DeleteTrack"))(track);
  std::ofstream(resource / "jsfx-controls-test.log", std::ios::app) << result << '\n';
  reinterpret_cast<void(*)(void*,const char*,int)>(host->GetFunc("Main_SaveProjectEx"))(nullptr, (resource / "jsfx-controls-test.rpp").c_str(), 8);
  reinterpret_cast<void(*)(void*,bool)>(host->GetFunc("Main_SaveProject"))(nullptr, false);
  reinterpret_cast<void(*)(int,int)>(host->GetFunc("Main_OnCommand"))(40004, 0);
}
void verify_observer() {
  const auto timing = observer->diagnostics();
  if (timing["hideControls"]["calls"].get<uint64_t>() > preparations) {
    host->Register("-timer", reinterpret_cast<void*>(verify_observer));
    finish("PASS: pre-paint discovery records inspection and preparation timing");
  } else if (reinterpret_cast<double(*)()>(host->GetFunc("time_precise"))() > observer_deadline) {
    host->Register("-timer", reinterpret_cast<void*>(verify_observer));
    finish("FAIL: pre-paint discovery did not record preparation timing: " + timing.dump());
  }
}
void test_observer() {
  fs::create_directories(resource / "Effects");
  std::ofstream(resource / "Effects/rwa-observer-probe") << "desc:rwa-observer-probe\nslider1:0<0,1,1>Value\n@gfx 300 200\ngfx_clear=0;\n";
  reinterpret_cast<void(*)(int,bool)>(host->GetFunc("InsertTrackAtIndex"))(0, false);
  track = reinterpret_cast<void*(*)(void*,int)>(host->GetFunc("GetTrack"))(nullptr, 0);
  observer_host.native_function = host->GetFunc;
  observer_host.current_project = [] { return reinterpret_cast<void*(*)(int,char*,int)>(host->GetFunc("EnumProjects"))(-1, nullptr, 0); };
  observer_host.valid_track = [](void* project, void* value) { return reinterpret_cast<bool(*)(void*,void*,const char*)>(host->GetFunc("ValidatePtr2"))(project, value, "MediaTrack*"); };
  observer_host.track_guid = [](void* value) { return *reinterpret_cast<Guid*(*)(void*)>(host->GetFunc("GetTrackGUID"))(value); };
  observer = make_jsfx_editor_discovery();
  observer->scan(observer_host, [](JsfxTarget target, int) -> std::optional<JsfxEditorDiscovery::Size> {
    if (match_editor && target.track == track) return JsfxEditorDiscovery::Size{300, 200};
    return {};
  });
  auto fx = reinterpret_cast<int(*)(void*,const char*,bool,int)>(host->GetFunc("TrackFX_AddByName"))(track, "rwa-observer-probe", false, -1);
  check(fx >= 0);
  reinterpret_cast<void(*)(void*,int,int)>(host->GetFunc("TrackFX_Show"))(track, fx, 3);
  auto window = reinterpret_cast<HWND(*)(void*,int)>(host->GetFunc("TrackFX_GetFloatingWindow"))(track, fx);
  check(window != nullptr);
  bool opened = true;
  static_cast<Discovery&>(*observer).Extended(CSURF_EXT_SETFXOPEN, track, &fx, &opened);
  HWND native_editor = nullptr;
  const auto find_editor = [](HWND candidate, LPARAM data) -> BOOL {
    const auto label = GetDlgItem(candidate, 1023), edit = GetDlgItem(candidate, 1062);
    if (label && edit && GetParent(label) == candidate && GetParent(edit) == candidate) {
      *reinterpret_cast<HWND*>(data) = candidate; return FALSE;
    }
    return TRUE;
  };
  if (find_editor(window, reinterpret_cast<LPARAM>(&native_editor)))
    EnumChildWindows(window, find_editor, reinterpret_cast<LPARAM>(&native_editor));
  check(native_editor != nullptr);
  preparations = observer->diagnostics()["hideControls"]["calls"];
  match_editor = true;
  SendMessage(native_editor, WM_SHOWWINDOW, TRUE, 0);
  observer_deadline = reinterpret_cast<double(*)()>(host->GetFunc("time_precise"))() + 3;
  host->Register("timer", reinterpret_cast<void*>(verify_observer));
}
void run() {
  host->Register("-timer", reinterpret_cast<void*>(run));
  std::string result;
  try {
    editor = CreateDialog(nullptr, MAKEINTRESOURCE(501), nullptr, procedure);
    check(editor != nullptr);
    SWELL_MakeSetCurParms(1, 1, 0, 0, editor, false, false);
    auto label = [](int id, int x, int y, int w, int h) { return SWELL_MakeLabel(0, "Control", id, x, y, w, h, 0); };
    const auto toolbar = label(1001, 10, 4, 200, 20);
    title = label(1023, 25, 65, 100, 20); content.push_back(title);
    content.push_back(SWELL_MakeButton(0, "Edit", 1062, 145, 65, 70, 20, 0));
    const auto left = label(1089, 2, 65, 12, 400), right = label(1091, 680, 65, 12, 400);
    const auto gfx = label(0, 25, 90, 300, 20), hidden_before = label(2001, 350, 90, 100, 20);
    ShowWindow(hidden_before, SW_HIDE);
    for (int i = 0; i < 32; ++i) content.push_back(label(3000+i, 25+(i%4)*155, 115+(i/4)*10, 145, 9));
    Attachment state;
    state.editor = state.owner = editor; state.prepared = true;
    state.conceal();
    for (auto control : content) check(control && !visible(control));
    check(visible(toolbar) && visible(left) && visible(right) && visible(gfx) && !visible(hidden_before));
    const auto count = state.hidden.size();
    original_rect = GetWindowRect; original_item = GetDlgItem;
    struct Restore {
      ~Restore() { GetWindowRect = original_rect; GetDlgItem = original_item; }
    } restore;
    GetWindowRect = tracked_rect; GetDlgItem = tracked_item;
    state.conceal();
    check(hidden_bounds == 0 && meter_lookups == 4 && state.hidden.size() == count);
    ShowWindow(content.back(), SW_SHOWNA);
    state.conceal();
    check(!visible(content.back()) && state.hidden.size() == count);
    const auto overlay = label(4000, 25, 90, 300, 350);
    state.child = overlay;
    state.conceal();
    check(visible(overlay) && !visible(gfx) && visible(toolbar) && visible(left) && visible(right));
    restore_native(state);
    for (auto control : content) check(visible(control));
    check(visible(gfx) && !visible(hidden_before) && state.hidden.empty());
    result = "PASS: hidden-control queries, bounded meter lookup, repeated concealment, toolbar/meters/overlay and native restoration";
  } catch (const std::exception& error) { result = std::string("FAIL: ") + error.what(); }
  if (editor) DestroyWindow(editor);
  std::ofstream(resource / "jsfx-controls-test.log") << result << '\n';
  if (result.rfind("PASS:", 0) == 0) {
    try { test_observer(); }
    catch (const std::exception& error) { finish(std::string("FAIL: ") + error.what()); }
  } else finish(result);
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) return 0;
  host = rec;
  resource = reinterpret_cast<const char*(*)()>(rec->GetFunc("GetResourcePath"))();
  if (!fs::exists(resource / "jsfx-controls-test.enabled")) return 0;
  rec->Register("timer", reinterpret_cast<void*>(run));
  return 1;
}
