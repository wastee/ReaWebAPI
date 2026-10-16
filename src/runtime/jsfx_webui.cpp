#include "runtime/jsfx_webui.hpp"
#include <cmath>
#include <cstring>
#include <fstream>

namespace reaweb {
namespace {
constexpr int max_parameters = 259; // 256 JSFX sliders plus REAPER's Bypass, Wet and Delta.
template<class T> T api(Host& host, const char* name) {
  auto result = host.native_function ? reinterpret_cast<T>(host.native_function(name)) : nullptr;
  if (!result) throw Error("HOST_UNAVAILABLE", std::string("JSFX Web UI requires ") + name);
  return result;
}
bool beneath(const fs::path& root, const fs::path& path) {
  auto relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
std::string config(Host& host, void* track, int fx, const std::string& key) {
  char buffer[4096]{};
  return api<bool (*)(void*, int, const char*, char*, int)>(host, "TrackFX_GetNamedConfigParm")(
    track, fx, key.c_str(), buffer, sizeof(buffer)) ? buffer : "";
}
}
std::optional<JsfxAssociation> jsfx_association(const fs::path& effects, const std::string& ident) {
  if (ident.empty() || ident.find('\0') != std::string::npos) return {};
  std::error_code error;
  const auto root = fs::weakly_canonical(effects, error);
  if (error) return {};
  auto source = fs::weakly_canonical(root / fs::u8path(ident), error);
  if (error || !beneath(root, source) || !fs::is_regular_file(source)) return {};
  auto sidecar = source; sidecar.replace_extension(".json");
  if (!fs::is_regular_file(sidecar) || fs::file_size(sidecar) > 16384) return {};
  std::ifstream input(sidecar);
  auto data = Json::parse(input, nullptr, false);
  if (!data.is_object() || data.value("schema", Json()) != 1 || !data.contains("JSFX_WebUI")) return {};
  const auto& ui = data["JSFX_WebUI"];
  if (!ui.is_object() || !ui.value("entry", Json()).is_string() || ui.value("enabled", Json(true)) != true) return {};
  auto entry = fs::weakly_canonical(source.parent_path() / fs::u8path(ui["entry"].get<std::string>()), error);
  if (error || !beneath(source.parent_path(), entry) || !fs::is_regular_file(entry) || entry.extension() != ".html") return {};
  const auto width = ui.value("width", Json(640)), height = ui.value("height", Json(420));
  if (!width.is_number_integer() || !height.is_number_integer() || width.get<double>() < 100 || width.get<double>() > 2048 ||
      height.get<double>() < 100 || height.get<double>() > 2048) return {};
  return JsfxAssociation{entry, ident, width.get<int>(), height.get<int>()};
}
JsfxBinding::JsfxBinding(Host& host, void* track, int fx) : host_(host), project_(host.current_project()), track_(track), index_(fx) {
  if (!host_.valid_track(project_, track_) || fx < 0 || fx >= api<int (*)(void*)>(host_, "TrackFX_GetCount")(track_))
    throw Error("FX_STALE", "JSFX instance is no longer available");
  track_guid_ = host_.track_guid(track_);
  const auto guid = api<Guid* (*)(void*, int)>(host_, "TrackFX_GetFXGUID")(track_, index_);
  if (!guid || config(host_, track_, index_, "fx_type") != "JS") throw Error("FX_STALE", "Expected a native JSFX instance");
  fx_guid_ = *guid;
  ident_ = config(host_, track_, index_, "fx_ident");
  generation_ = host_.project_generation ? host_.project_generation() : 0;
}
int JsfxBinding::resolve() {
  if (host_.current_project() != project_ || (host_.project_generation && host_.project_generation() != generation_) ||
      !host_.valid_track(project_, track_) || host_.track_guid(track_) != track_guid_)
    throw Error("FX_STALE", "JSFX project or track identity changed");
  const auto count = api<int (*)(void*)>(host_, "TrackFX_GetCount")(track_);
  const auto guid = api<Guid* (*)(void*, int)>(host_, "TrackFX_GetFXGUID");
  const auto matches = [&](int i) { auto g = guid(track_, i); return g && *g == fx_guid_; };
  if (index_ < 0 || index_ >= count || !matches(index_)) {
    index_ = -1;
    for (int i = 0; i < count && i < 4096; ++i) if (matches(i)) { index_ = i; break; }
  }
  if (index_ < 0 || config(host_, track_, index_, "fx_type") != "JS" || config(host_, track_, index_, "fx_ident") != ident_)
    throw Error("FX_STALE", "JSFX instance was removed or replaced");
  return index_;
}
bool JsfxBinding::editor_matches(const JsfxEditor& editor) {
  const auto fx = resolve();
  if (editor.track != track_ || !editor.host || !editor.host->valid()) return false;
  if (!editor.floating) {
    const auto selected = editor.host->selected_fx();
    const bool matches = (selected ? *selected : api<int (*)(void*)>(host_, "TrackFX_GetChainVisible")(track_)) == fx;
    if (matches) editor.host->sync_selection(fx);
    return matches;
  }
  return api<void* (*)(void*, int)>(host_, "TrackFX_GetFloatingWindow")(track_, fx) == editor.container;
}
std::string JsfxBinding::ident() { return config(host_, track_, resolve(), "fx_ident"); }
int JsfxBinding::parameter_count() {
  const int count = api<int (*)(void*, int)>(host_, "TrackFX_GetNumParams")(track_, resolve());
  if (count < 0 || count > max_parameters) throw Error("FX_LIMIT", "JSFX Web UI supports at most 259 host parameters");
  return count;
}
Json JsfxBinding::parameter(int param, bool metadata) {
  const auto fx = resolve();
  const auto count = api<int (*)(void*, int)>(host_, "TrackFX_GetNumParams")(track_, fx);
  if (param < 0 || param >= count || count > max_parameters) throw Error("INVALID_ARGUMENT", "Invalid JSFX parameter index");
  double minimum = 0, maximum = 0, midpoint = 0;
  const auto value = api<double (*)(void*, int, int, double*, double*, double*)>(host_, "TrackFX_GetParamEx")(
    track_, fx, param, &minimum, &maximum, &midpoint);
  const auto normalized = api<double (*)(void*, int, int)>(host_, "TrackFX_GetParamNormalized")(track_, fx, param);
  char text[1024]{};
  const auto string = [&](const char* name) -> Json {
    text[0] = 0;
    return api<bool (*)(void*, int, int, char*, int)>(host_, name)(track_, fx, param, text, sizeof(text)) ? Json(text) : Json();
  };
  Json result{{"index", param}, {"value", value}, {"normalized", normalized}, {"formatted", string("TrackFX_GetFormattedParamValue")}};
  if (!metadata) return result;
  result.update({{"name", string("TrackFX_GetParamName")}, {"ident", string("TrackFX_GetParamIdent")},
    {"min", minimum}, {"max", maximum}, {"mid", midpoint}, {"defaultNormalized", nullptr}, {"section", nullptr},
    {"step", nullptr}, {"smallStep", nullptr}, {"largeStep", nullptr}, {"toggle", nullptr}, {"enum", nullptr}});
  auto default_value = config(host_, track_, fx, "param." + std::to_string(param) + ".default_value");
  if (!default_value.empty()) {
    char* end = nullptr; const auto d = std::strtod(default_value.c_str(), &end);
    if (end && !*end && std::isfinite(d) && d >= 0 && d <= 1) result["defaultNormalized"] = d;
  }
  double step = 0, small = 0, large = 0; bool toggle = false;
  if (api<bool (*)(void*, int, int, double*, double*, double*, bool*)>(host_, "TrackFX_GetParameterStepSizes")(
      track_, fx, param, &step, &small, &large, &toggle))
    result.update({{"step", step}, {"smallStep", small}, {"largeStep", large}, {"toggle", toggle}});
  return result;
}
Json JsfxBinding::state() {
  Json parameters = Json::array();
  const auto count = parameter_count();
  for (int p = 0; p < count; ++p) parameters.push_back(parameter(p, true));
  return {{"parameters", parameters}, {"count", count}, {"fxIdent", ident()}};
}
void JsfxBinding::set(int param, double value, bool normalized) {
  auto state = parameter(param, true);
  const auto minimum = normalized ? 0.0 : state["min"].get<double>(), maximum = normalized ? 1.0 : state["max"].get<double>();
  if (!std::isfinite(value) || value < minimum || value > maximum) throw Error("INVALID_ARGUMENT", "JSFX parameter value is outside its range");
  if (!api<bool (*)(void*, int, int, double)>(host_, normalized ? "TrackFX_SetParamNormalized" : "TrackFX_SetParam")(
      track_, resolve(), param, value)) throw Error("FX_WRITE_FAILED", "REAPER rejected the parameter value");
}
void JsfxBinding::end(int param) {
  const auto fx = resolve();
  if (param >= 0 && param < parameter_count()) api<bool (*)(void*, int, int)>(host_, "TrackFX_EndParamEdit")(track_, fx, param);
}
}
