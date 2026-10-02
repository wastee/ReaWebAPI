#pragma once
#include "core/core.hpp"

namespace reaweb {
// Created, sampled and destroyed on the host thread, like Track AudioAccessors.
class AggregateSource {
public:
  AggregateSource(const Host& host, void* root);
  ~AggregateSource();
  int read(int rate, int channels, double position, int frames, double* output);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
