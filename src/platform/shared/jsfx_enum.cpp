#include "platform/shared/jsfx_enum.hpp"
#include "runtime/jsfx_enum.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <swell/swell.h>
#include <dlfcn.h>
#endif

namespace reaweb {
void jsfx_editor_enum_metadata(void* editor, Json& parameters) {
  const auto window = static_cast<HWND>(editor);
  if (!window || !IsWindow(window)) return;
#ifdef _WIN32
  // JSFX uses an ANSI dialog. The W getter may return a system conversion handle.
  const auto procedure = GetWindowLongPtrA(window, DWLP_DLGPROC);
  HMODULE module = nullptr;
  if (!procedure || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      reinterpret_cast<LPCWSTR>(procedure), &module)) return;
  const auto api = reinterpret_cast<const JsfxEngineApi*>(GetProcAddress(module, "JesusonicAPI"));
#else
  const auto procedure = GetWindowLong(window, DWL_DLGPROC);
  Dl_info info{};
  if (!procedure || !dladdr(reinterpret_cast<void*>(procedure), &info) || !info.dli_fname) return;
  const auto module = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
  if (!module) return;
  const auto api = reinterpret_cast<const JsfxEngineApi*>(dlsym(module, "JesusonicAPI"));
  // dlsym can search dependencies. The dialog and API must belong to one module.
  Dl_info api_info{};
  const bool same_module = api && dladdr(api, &api_info) && api_info.dli_fbase == info.dli_fbase;
#endif
  const auto close = [](void* handle) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
  };
  const std::unique_ptr<void, decltype(close)> owner(module, close);
  if (api && api->version == jsfx_engine_version && api->get_ui
#ifndef _WIN32
      && same_module
#endif
      ) {
    // The inspected JSFX dialog stores its SX_Instance in USERDATA at WM_INITDIALOG.
    // Never retain this pointer after the synchronous main-thread snapshot.
#ifdef _WIN32
    auto instance = reinterpret_cast<void*>(GetWindowLongPtr(window, GWLP_USERDATA));
#else
    auto instance = reinterpret_cast<void*>(GetWindowLong(window, GWL_USERDATA));
#endif
    if (instance && api->get_ui(instance) == editor) jsfx_enum_metadata(*api, instance, parameters);
  }
}
}
