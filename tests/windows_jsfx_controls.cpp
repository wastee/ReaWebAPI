#include "../src/platform/shared/jsfx_editor.cpp"
#include <iostream>

using namespace reaweb;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)

int main() {
  try {
    auto owner = CreateWindowW(L"STATIC", L"Control visibility regression", WS_OVERLAPPEDWINDOW,
      0, 0, 900, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(owner);
    struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{owner};
    auto editor = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 800, 400,
      owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(editor);
    const auto control = [&](int id, int x, int y, int width, int height, bool visible = true) {
      auto child = CreateWindowW(L"STATIC", L"", WS_CHILD | (visible ? WS_VISIBLE : 0), x, y, width, height,
        editor, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
      CHECK(child); return child;
    };
    const auto visible = [](HWND window) { return (GetWindowLong(window, GWL_STYLE) & WS_VISIBLE) != 0; };
    const auto toolbar = control(1001, 10, 0, 100, 20);
    const auto label = control(1023, 20, 60, 100, 20);
    const auto edit = control(1062, 150, 60, 80, 20);
    const auto meter = control(1089, 0, 60, 10, 260);
    const auto hidden_before = control(2001, 100, 200, 80, 20, false);
    const auto gfx = control(0, 10, 90, 400, 200);
    std::vector<HWND> sliders;
    for (int i = 0; i < 24; ++i) sliders.push_back(control(3000+i, 20, 100+i*8, 80, 7));
    Attachment state;
    state.editor = editor; state.owner = owner; state.prepared = true;
    state.conceal();
    CHECK(visible(toolbar) && visible(meter) && visible(gfx));
    CHECK(!visible(label) && !visible(edit) && !visible(hidden_before));
    for (auto slider : sliders) CHECK(!visible(slider));
    const auto hidden_count = state.hidden.size();
    state.conceal(); CHECK(state.hidden.size() == hidden_count);
    ShowWindow(sliders.front(), SW_SHOWNA);
    state.conceal(); CHECK(!visible(sliders.front()) && state.hidden.size() == hidden_count);
    restore_native(state);
    CHECK(visible(label) && visible(edit) && visible(toolbar) && visible(meter) && !visible(hidden_before));
    for (auto slider : sliders) CHECK(visible(slider));
    CHECK(state.hidden.empty());

    const auto overlay = control(4000, 10, 90, 400, 200);
    state.child = overlay; state.prepared = true;
    RECT before{}, after{}; GetWindowRect(overlay, &before);
    state.conceal(); GetWindowRect(overlay, &after);
    CHECK(visible(overlay) && visible(toolbar) && visible(meter) && !visible(gfx));
    CHECK(EqualRect(&before, &after));
    restore_native(state);
    CHECK(visible(gfx) && visible(label) && !visible(hidden_before));
    {
      ShowWindow(owner, SW_SHOWNOACTIVATE);
      Guid fx_guid{1};
      Host native_host;
      native_host.native_function = [](const char* name) -> void* {
        if (std::strcmp(name, "TrackFX_GetFXGUID")) return nullptr;
        return reinterpret_cast<void*>(+[](void* track, int fx) -> Guid* {
          CHECK(fx == 0);
          return static_cast<Guid*>(track);
        });
      };
      EditorHost host(editor, owner, nullptr, native_host, JsfxTarget{&fx_guid}, 0);
      host.prepare();
      DWORD cloaked = 0;
      CHECK(SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))));
      CHECK(cloaked & DWM_CLOAKED_APP);
      CHECK(!visible(label) && visible(toolbar) && visible(meter));
      host.reveal();
      CHECK(SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))));
      CHECK(!(cloaked & DWM_CLOAKED_APP));
      host.restore();
      CHECK(visible(label) && visible(toolbar) && !visible(hidden_before));
    }
    std::cout << "PASS: JSFX control concealment, toolbar/meters/overlay preservation, repeated concealment, native restoration and cloak/reveal guards\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
