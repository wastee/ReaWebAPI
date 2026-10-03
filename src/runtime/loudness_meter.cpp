#include "runtime/loudness_meter.hpp"
#include <algorithm>
#include <cmath>

namespace reaweb {
namespace {
constexpr double pi = 3.14159265358979323846;
double loudness(double energy) { return -.691 + 10 * std::log10(energy); }
int bin(double db) { return static_cast<int>((db + 70) * 10); }
}
double CockosMeter::Filter::process(double value, double& p1, double& p2) const {
  const double p0 = value - a1 * p1 - a2 * p2;
  const double result = b0 * p0 + b1 * p1 + b2 * p2;
  p2 = p1; p1 = p0; return result;
}
CockosMeter::CockosMeter(unsigned rate, unsigned channels)
  : channels_(channels), slices_(rate < 96000 ? 3 : 1), step_frames_(std::max(1u, (rate + 5) / 10)), integrated_(-INFINITY) {
  for (unsigned slice = 0; slice < slices_; ++slice) for (unsigned i = 0; i < 32; ++i) {
    const double position = i + (slices_ == 1 ? .5 : (slice + 1) * .25);
    const double at = pi * (position - 16);
    sinc_[slice][i] = (.53836 - std::cos(2 * pi / 32 * position) * .46164) * std::sin(at) / at;
  }
  for (unsigned ch = 0; ch < channels; ++ch)
    channels_[ch].weight = channels < 6 || ch < 3 ? 1 : ch == 3 ? 0 : std::sqrt(2.);
  double k = std::tan(pi * 1681.974450955533 / rate), q = .7071752369554196;
  const double vh = std::pow(10., 3.999843853973347 / 20), vb = std::pow(vh, .4996667741545416);
  const double a0 = 1 + k / q + k * k;
  filters_[0] = {2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0,
    (vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0};
  k = std::tan(pi * 38.13547087602444 / rate); q = .5003270373238773;
  filters_[1] = {2 * (k * k - 1) / (1 + k / q + k * k), (1 - k / q + k * k) / (1 + k / q + k * k), 1, -2, 1};

}
double CockosMeter::sample(unsigned channel, double value) {
  auto& state = channels_[channel];
  state.head = (state.head + 31) % 32; state.history[state.head] = value;
  double peak = std::abs(state.history[(state.head + 16) % 32]);
  for (unsigned slice = 0; slice < slices_; ++slice) {
    double interpolated = 0;
    for (unsigned i = 0; i < 32; ++i) interpolated += state.history[(state.head + i) % 32] * sinc_[slice][i];
    peak = std::max(peak, std::abs(interpolated));
  }
  state.maximum = std::max(state.maximum, peak);
  if (peak > 1 && state.clips != UINT64_MAX) ++state.clips;
  value = filters_[0].process(value * state.weight, state.p1, state.p2);
  value = filters_[1].process(value, state.q1, state.q2);
  state.energy += value * value;
  return peak;
}
void CockosMeter::step(double rms_energy, bool integrate) {
  double energy = 0;
  for (auto& channel : channels_) { energy += channel.energy; channel.energy = 0; }
  energies_[position_] = energy / step_frames_;
  double momentary = 0, short_term = 0;
  for (unsigned i = 0; i < energies_.size(); ++i) {
    const auto block = energies_[(position_ + energies_.size() - i) % energies_.size()];
    if (i < 4) momentary += block;
    short_term += block;
  }
  momentary /= 4; short_term /= 30;
  position_ = (position_ + 1) % energies_.size(); ++steps_;
  momentary_ = steps_ >= 4 && momentary > 0 ? loudness(momentary) : -INFINITY;
  short_term_ = steps_ >= 30 && short_term > 0 ? loudness(short_term) : -INFINITY;
  if (integrate) { rms_sum_ += rms_energy; ++rms_count_; }
  if (integrate && steps_ >= 4 && momentary > 0) {
    const auto index = bin(loudness(momentary));
    if (index >= 0) {
      auto& entry = integrated_hist_[std::min(index, 1023)];
      ++entry.count; entry.energy += momentary;
      integrated_sum_ += momentary; ++integrated_count_;
      const auto gate = std::clamp(bin(loudness(integrated_sum_ / integrated_count_) - 10), 0, 1024);
      double sum = 0; uint64_t count = 0;
      for (size_t i = gate; i < integrated_hist_.size(); ++i) { sum += integrated_hist_[i].energy; count += integrated_hist_[i].count; }
      integrated_ = sum > 0 && count ? loudness(sum / count) : -INFINITY;
    }
  }
  if (integrate && steps_ >= 30 && short_term > 0) {
    const auto index = bin(short_term_);
    if (index >= 0) {
      ++range_hist_[std::min(index, 1023)]; range_sum_ += short_term; ++range_count_;
      const int gate = std::clamp(bin(loudness(range_sum_ / range_count_) - 20), 0, 1024);
      uint64_t count = 0;
      for (int i = gate; i < 1024; ++i) count += range_hist_[i];
      if (count >= 20) {
        uint64_t low_count = 0, high_count = 0;
        int low = gate, high = 1023;
        while (low < 1024 && low_count < count * .10) low_count += range_hist_[low++];
        while (high >= gate && high_count < count * .05) high_count += range_hist_[high--];
        range_low_ = (low - 1) * .1 - 70;
        range_high_ = (high + 1) * .1 - 70;
      }
    }
  }
}

double CockosMeter::rms_integrated() const {
  return rms_count_ >= 4 && rms_sum_ > 0 ? 10 * std::log10(rms_sum_ / rms_count_) : -INFINITY;
}
}
