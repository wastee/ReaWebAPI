#pragma once
#include "core/core.hpp"
#include <array>
#include <chrono>

namespace reaweb {
// Main-thread observations. Values are offsets, not additive phase durations.
class StartupTiming {
public:
  using Clock = std::chrono::steady_clock;
  enum Stage { Open, EnvironmentWait, EnvironmentReady, ControllerStart, ControllerReady,
    Navigation, Loaded, Handshake, FrameRequested, FrameReady, Revealed,
    Discovered, Connected, Parameters, EditorSize, SizeRequested, Present,
    CapabilitiesReady, ParametersStart, EditorSizeStart, Count };
private:
  Clock::time_point origin_;
  std::array<double, Count> marks_;
  const char* kind_ = "create";
public:
  explicit StartupTiming(Clock::time_point origin = Clock::now()) { reset("create", origin); }
  void reset(const char* kind, Clock::time_point origin = Clock::now()) {
    origin_ = origin; kind_ = kind; marks_.fill(-1); marks_[Open] = 0;
  }
  void mark(Stage stage, Clock::time_point at = Clock::now()) {
    if (marks_[stage] < 0) marks_[stage] = std::chrono::duration<double, std::milli>(at - origin_).count();
  }
  Json snapshot() const {
    static const char* names[] = {"openMs", "environmentWaitStartMs", "environmentReadyMs", "controllerStartMs",
      "controllerReadyMs", "navigationMs", "loadCompleteMs", "runtimeHandshakeMs", "frameRequestMs", "frameReadyMs",
      "revealCompleteMs", "fxDiscoveredMs", "connectMs", "parametersReadyMs", "editorSizeReadyMs", "sizeRequestMs", "presentMs",
      "capabilitiesReadyMs", "parametersStartMs", "editorSizeStartMs"};
    Json marks = Json::object();
    for (size_t i = 0; i < Count; ++i) if (marks_[i] >= 0) marks[names[i]] = marks_[i];
    return {{"kind", kind_}, {"clock", "native-steady"}, {"milestones", std::move(marks)}};
  }
};
}
