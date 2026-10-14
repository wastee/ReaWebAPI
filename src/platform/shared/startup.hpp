#pragma once
#include <chrono>

namespace reaweb {
// Visibility intent survives asynchronous first-frame preparation and native reparenting.
class StartupPresentation {
  using Clock = std::chrono::steady_clock;
  Clock::time_point started_ = Clock::now();
  bool pending_ = true, wanted_ = true, ready_ = false, cancelled_ = false;
  bool explicit_ = false, focus_ = true;
  unsigned generation_ = 0;
public:
  explicit StartupPresentation(bool hidden = false) : wanted_(!hidden), focus_(!hidden) {}
  bool pending() const { return pending_; }
  bool wanted() const { return wanted_; }
  void request(bool visible) { if (!cancelled_) wanted_ = visible; }
  void show(bool focus = false) { if (!cancelled_) { wanted_ = true; explicit_ = true; focus_ = focus; } }
  bool focus_requested() const { return focus_; }
  bool cancelled() const { return cancelled_; }
  void cancel() { cancelled_ = true; wanted_ = false; }
  unsigned navigate() { ready_ = false; return ++generation_; }
  unsigned generation() const { return generation_; }
  void prepared(unsigned generation) { if (generation == generation_) ready_ = true; }
  bool present() {
    if (!pending_ || cancelled_ || !wanted_ || (!ready_ && !explicit_)) return false;
    pending_ = false;
    return true;
  }
  bool expired() const { return pending_ && wanted_ && !cancelled_ && !ready_ && !explicit_ && Clock::now() - started_ >= std::chrono::seconds(15); }
};
}
