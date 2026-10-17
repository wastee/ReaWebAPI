#include "runtime/jsfx_webui.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace reaweb;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
struct Fx { Guid guid; std::string type = "JS"; double raw = -42; std::vector<int> children; };
struct Owner { Guid guid; std::map<int, Fx> fx; bool valid = true; Owner* track = nullptr; Owner* item = nullptr; };
Owner track{Guid{1}}, master{Guid{2}}, item{Guid{3}}, take{Guid{4}}, other_take{Guid{5}};
int project, writes = 0, ends = 0;
bool expect_take = false;
Fx& effect(void* owner, int fx) { return static_cast<Owner*>(owner)->fx.at(fx); }
void* native(const char* name) {
#define FN(n, f) if (!std::strcmp(name, n)) return reinterpret_cast<void*>(+f)
  FN("ValidatePtr2", [](void*, void* p, const char*) { return p && static_cast<Owner*>(p)->valid; });
  FN("GetMediaItemTake_Item", [](void* p) -> void* { return static_cast<Owner*>(p)->item; });
  FN("GetMediaItem_Track", [](void* p) -> void* { return static_cast<Owner*>(p)->track; });
  if (!std::strcmp(name, "GetSetMediaItemInfo") || !std::strcmp(name, "GetSetMediaItemTakeInfo"))
    return reinterpret_cast<void*>(+[](void* p, const char*, void*) -> void* { return &static_cast<Owner*>(p)->guid; });
  if (!std::strncmp(name, "TrackFX_", 8)) { CHECK(!expect_take); name += 8; }
  else if (!std::strncmp(name, "TakeFX_", 7)) { CHECK(expect_take); name += 7; }
  else return nullptr;
  FN("GetCount", [](void*) { return 2; });
  FN("GetRecCount", [](void*) { return 2; });
  FN("GetFXGUID", [](void* p, int fx) -> Guid* { auto& m = static_cast<Owner*>(p)->fx; auto i = m.find(fx); return i == m.end() ? nullptr : &i->second.guid; });
  FN("GetNamedConfigParm", [](void* p, int fx, const char* key, char* out, int) {
    const auto& value = effect(p, fx); std::string text;
    if (!std::strcmp(key, "fx_type")) text = value.type;
    else if (!std::strcmp(key, "fx_ident")) text = "Utility/test.jsfx";
    else if (!std::strcmp(key, "container_count") && value.type == "Container") text = std::to_string(value.children.size());
    else if (!std::strncmp(key, "container_item.", 15)) text = std::to_string(value.children.at(std::stoi(key + 15)));
    else if (!std::strcmp(key, "param.0.default_value")) text = "0.5";
    else return false;
    std::strcpy(out, text.c_str()); return true;
  });
  FN("GetNumParams", [](void*, int) { return 1; });
  FN("GetParamEx", [](void* p, int f, int, double* lo, double* hi, double* mid) { *lo = -60; *hi = 12; *mid = -42; return effect(p, f).raw; });
  FN("GetParamNormalized", [](void* p, int f, int) { return std::sqrt((effect(p, f).raw + 60) / 72); });
  if (!std::strcmp(name, "GetParamName") || !std::strcmp(name, "GetParamIdent") || !std::strcmp(name, "GetFormattedParamValue"))
    return reinterpret_cast<void*>(+[](void*, int, int, char* out, int) { std::strcpy(out, "Gain"); return true; });
  FN("GetParameterStepSizes", [](void*, int, int, double*, double*, double*, bool*) { return false; });
  FN("SetParamNormalized", [](void* p, int f, int, double v) { effect(p, f).raw = -60 + 72*v*v; ++writes; return true; });
  FN("SetParam", [](void* p, int f, int, double v) { effect(p, f).raw = v; ++writes; return true; });
  FN("EndParamEdit", [](void*, int, int) { ++ends; return true; });
#undef FN
  return nullptr;
}
template<class F> void stale(F fn) { const int before = writes; bool rejected = false; try { fn(); } catch (const Error& e) { rejected = e.code == "FX_STALE"; } CHECK(rejected && writes == before); }
void populate(Owner& o, int mask) {
  o.fx[mask] = {Guid{10}, "Container", -42, {mask | 0x2000004}};
  o.fx[mask | 1] = {Guid{11}};
  o.fx[mask | 0x2000004] = {Guid{12}, "Container", -42, {mask | 0x200000a}};
  o.fx[mask | 0x200000a] = {Guid{13}};
}
}
int main() {
  try {
    Host h; h.current_project = []() -> void* { return &project; };
    h.valid_track = [](void*, void* p) { return (p == &track || p == &master) && static_cast<Owner*>(p)->valid; };
    h.track_guid = [](void* p) { return static_cast<Owner*>(p)->guid; }; h.native_function = native;
    item.track = &track; take.item = other_take.item = &item;
    for (const auto target : {JsfxTarget{&track}, JsfxTarget{&master}, JsfxTarget{&track, nullptr, true},
                             JsfxTarget{&master, nullptr, true}, JsfxTarget{&track, &take}}) {
      expect_take = target.take != nullptr;
      auto& owner = *static_cast<Owner*>(target.object()); owner.fx.clear();
      const int mask = target.input ? 0x1000000 : 0;
      populate(owner, mask);
      JsfxBinding binding(h, target, mask | 0x200000a);
      CHECK(binding.parameter(0, true)["defaultNormalized"] == .5);
      binding.set(0, .75, true); CHECK(std::abs(binding.parameter(0)["value"].get<double>() + 19.5) < 1e-9);
      binding.set(0, -24, false); CHECK(binding.parameter(0)["value"] == -24);
      CHECK(owner.fx[mask | 1].raw == -42);
      // Reordering and moving out of a nested container must keep the GUID, not the old slot.
      std::swap(owner.fx[mask | 1], owner.fx[mask | 0x200000a]);
      CHECK(binding.resolve() == (mask | 1)); binding.set(0, -12, false);
      CHECK(owner.fx[mask | 1].raw == -12 && owner.fx[mask | 0x200000a].raw == -42);
      binding.end(0);
      owner.fx[mask | 1].guid = Guid{99}; stale([&] { binding.set(0, -10, false); });
      owner.fx[mask | 1].guid = Guid{13}; CHECK(binding.resolve() == (mask | 1));
      if (target.take) {
        item.track = &master; stale([&] { binding.set(0, 0, false); }); item.track = &track;
        take.guid = Guid{77}; stale([&] { binding.set(0, 0, false); }); take.guid = Guid{4};
        item.valid = false; stale([&] { binding.set(0, 0, false); }); item.valid = true;
        other_take.fx = owner.fx;
        JsfxBinding other(h, JsfxTarget{&track, &other_take}, mask | 1);
        other.set(0, -6, false); CHECK(binding.parameter(0)["value"] == -12);
      }
    }
    CHECK(ends == 5);
    std::cout << "Track/master/input/monitor/take nested identity and shaped raw parameters passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
