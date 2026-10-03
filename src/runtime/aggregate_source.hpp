#pragma once
#include "core/core.hpp"
#include <chrono>

namespace reaweb {
// Created, sampled and destroyed on the host thread, like Track AudioAccessors.
class AggregateSource {
public:
  AggregateSource(const Host& host, void* root);
  ~AggregateSource();
  // Returns 2 while a bounded read is pending. Partial sums are never exposed.
  int read(int rate, int channels, double position, int frames, double* output,
           std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
  uint64_t generation() const;
  unsigned channels();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
