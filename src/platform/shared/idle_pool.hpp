#pragma once
#include "core/core.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

namespace reaweb {
inline constexpr unsigned default_idle_webviews = 2, max_idle_webviews = 32;

// Only unassigned containers enter this pool. Application windows are never returned.
template<class T> class IdlePool {
public:
  using Clock = std::chrono::steady_clock;
private:
  struct Entry { std::shared_ptr<T> value; Clock::time_point started; };
  std::vector<Entry> entries_;
  unsigned target_ = 0;
  uint64_t created_ = 0, taken_ = 0, failed_ = 0;
  Clock::time_point next_{};
public:
  void capacity(unsigned count) {
    if (count > max_idle_webviews) throw Error("INVALID_ARGUMENT", "Idle WebView count must be 0..32");
    target_ = count;
    if (entries_.size() > count) entries_.resize(count);
  }
  std::shared_ptr<T> take() {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (!it->value->ready || it->value->failed) continue;
      auto value = std::move(it->value); entries_.erase(it); ++taken_;
      next_ = Clock::now() + std::chrono::milliseconds(100);
      return value;
    }
    return {};
  }
  template<class Create> void maintain(Create create, Clock::time_point now = Clock::now()) {
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (it->value->failed || (!it->value->ready && now - it->started >= std::chrono::seconds(15))) {
        it = entries_.erase(it); ++failed_; next_ = now + std::chrono::seconds(1);
      } else ++it;
    }
    if (entries_.size() >= target_ || now < next_ ||
        std::any_of(entries_.begin(), entries_.end(), [](const auto& item) { return !item.value->ready; })) return;
    next_ = now + std::chrono::milliseconds(100);
    try { entries_.push_back({create(), now}); ++created_; }
    catch (const std::exception&) { ++failed_; next_ = now + std::chrono::seconds(1); }
  }
  Json diagnostics() const {
    const auto ready = std::count_if(entries_.begin(), entries_.end(), [](const auto& item) { return item.value->ready && !item.value->failed; });
    return {{"target", target_}, {"ready", ready}, {"pending", entries_.size() - ready},
      {"created", created_}, {"taken", taken_}, {"failed", failed_}};
  }
};
}
