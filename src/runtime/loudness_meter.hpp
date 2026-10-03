#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

namespace reaweb {
// Cockos Loudness Meter DSP, Copyright (C) 2021 and later Cockos Incorporated.
// Adapted from analysis/loudness_meter under the GNU LGPL. See licenses/loudness_meter.txt.
class CockosMeter {
public:
  CockosMeter(unsigned rate, unsigned channels);
  double sample(unsigned channel, double value);
  void step(double rms_energy, bool integrate);
  double lufs_momentary() const { return momentary_; }
  double lufs_short_term() const { return short_term_; }
  double true_peak(unsigned channel) const { return channels_[channel].maximum; }
  uint64_t true_clips(unsigned channel) const { return channels_[channel].clips; }
  double rms_integrated() const;
  double lufs_integrated() const { return integrated_; }
  double range_low() const { return range_low_; }
  double range_high() const { return range_high_; }
private:
  struct Filter {
    double a1 = 0, a2 = 0, b0 = 0, b1 = 0, b2 = 0;
    double process(double value, double& p1, double& p2) const;
  } filters_[2];
  struct Channel {
    std::array<double, 32> history{};
    unsigned head = 0;
    double weight = 1, p1 = 0, p2 = 0, q1 = 0, q2 = 0, energy = 0, maximum = 0;
    uint64_t clips = 0;
  };
  struct Bin { uint64_t count = 0; double energy = 0; };
  std::vector<Channel> channels_;
  std::array<std::array<double, 32>, 3> sinc_{};
  unsigned slices_, step_frames_, position_ = 0;
  uint64_t steps_ = 0, rms_count_ = 0, integrated_count_ = 0;
  std::array<double, 30> energies_{};
  std::array<Bin, 1024> integrated_hist_{};
  std::array<uint64_t, 1024> range_hist_{};
  uint64_t range_count_ = 0;
  double range_sum_ = 0;
  double rms_sum_ = 0, integrated_sum_ = 0;
  double momentary_ = -INFINITY, short_term_ = -INFINITY;
  double integrated_, range_low_ = -100, range_high_ = -100;
};
}
