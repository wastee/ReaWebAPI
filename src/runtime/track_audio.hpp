#pragma once
#include "runtime/native_stream.hpp"

namespace reaweb {
// Main-thread owner of a shared, pass-through track FX capture point.
class TrackAudio {
public:
  TrackAudio(const Host& host, void* track, unsigned slot, uint64_t token);
  ~TrackAudio();
  bool valid();
  bool read(std::vector<float>& samples, unsigned& rate, uint64_t& sequence, double& position);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
