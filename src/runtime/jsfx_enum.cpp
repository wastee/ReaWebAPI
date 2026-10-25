#include "runtime/jsfx_enum.hpp"
#include <cmath>
#include <cstring>

namespace reaweb {
void jsfx_enum_metadata(const JsfxEngineApi& api, void* instance, Json& parameters) {
  if (!instance || api.version != jsfx_engine_version || !api.count || !api.get || !api.display || !api.is_enum) return;
  const auto count = api.count(instance);
  // REAPER appends Bypass, Wet and Delta; these are not JSFX engine parameters.
  if (count < 0 || count > 256 || parameters.size() != static_cast<size_t>(count + 3)) return;
  const auto base_bytes = parameters.dump().size();
  constexpr size_t envelope_bytes = 32 * 1024;
  if (base_bytes >= host_message_limit - envelope_bytes) return;
  size_t remaining_json = host_message_limit - envelope_bytes - base_bytes;
  size_t remaining_options = 4096, remaining_bytes = 128 * 1024;
  for (int index = 0; index < count; ++index) {
    auto& parameter = parameters[index];
    // The engine returns the last option index, or 1 for a singleton, not a count.
    const int flag = api.is_enum(instance, index);
    if (flag <= 0 || flag >= 1024) continue;
    double minimum = 0, maximum = 0, step = 0;
    api.get(instance, index, &minimum, &maximum, &step);
    if (minimum != 0 || !std::isfinite(maximum) || maximum < 0 || maximum >= 1024 ||
        std::floor(maximum) != maximum || step != 1 || flag != (maximum == 0 ? 1 : int(maximum)) ||
        parameter["min"] != minimum ||
        (parameter["max"] != maximum && !(maximum == 0 && parameter["max"] == 1))) continue;
    const auto options = static_cast<size_t>(maximum) + 1;
    if (options > remaining_options) continue;
    remaining_options -= options;
    Json result = Json::array();
    for (size_t value = 0; value < options; ++value) {
      char label[4096]{};
      double raw = double(value);
      api.display(instance, index, label, sizeof(label), &raw);
      const auto end = static_cast<const char*>(std::memchr(label, 0, sizeof(label)));
      if (!end || end - label == sizeof(label) - 1 || size_t(end - label) > remaining_bytes) break;
      remaining_bytes -= size_t(end - label);
      // Reject incomplete UTF-8 instead of making the entire snapshot unserializable.
      try { (void)Json(label).dump(); } catch (const Json::exception&) { break; }
      result.push_back({{"value", value}, {"label", label}});
    }
    if (result.size() == options) {
      const auto bytes = result.dump().size();
      if (bytes > remaining_json) continue;
      remaining_json -= bytes;
      parameter["enum"] = std::move(result);
    }
  }
}
}
