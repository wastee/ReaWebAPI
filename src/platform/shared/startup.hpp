#pragma once
#include <chrono>

namespace reaweb {
// Visibility intent survives asynchronous first-frame preparation and native reparenting.
class StartupPresentation {
  using Clock = std::chrono::steady_clock;
  Clock::time_point started_ = Clock::now();
  bool pending_ = true, wanted_ = true, ready_ = false, cancelled_ = false;
  unsigned generation_ = 0;
public:
  bool pending() const { return pending_; }
  bool wanted() const { return wanted_; }
  void request(bool visible) { wanted_ = visible; }
  void cancel() { cancelled_ = true; wanted_ = false; }
  unsigned navigate() { ready_ = false; return ++generation_; }
  unsigned generation() const { return generation_; }
  void prepared(unsigned generation) { if (generation == generation_) ready_ = true; }
  bool present() {
    if (!pending_ || cancelled_ || !wanted_ || !ready_) return false;
    pending_ = false;
    return true;
  }
  bool expired() const { return pending_ && !cancelled_ && !ready_ && Clock::now() - started_ >= std::chrono::seconds(15); }
};
}
