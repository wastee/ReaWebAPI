#include "runtime/audio_analysis.hpp"
#include "runtime/loudness_meter.hpp"
#include <ebur128.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>

namespace reaweb {
namespace {
constexpr double pi = 3.14159265358979323846;
void fft(std::vector<std::complex<double>>& data) {
  const auto count = data.size();
  for (size_t i = 1, j = 0; i < count; ++i) {
    size_t bit = count >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) std::swap(data[i], data[j]);
  }
  for (size_t length = 2; length <= count; length <<= 1) {
    const auto step = std::polar(1.0, -2 * pi / length);
    for (size_t start = 0; start < count; start += length) {
      std::complex<double> phase = 1;
      for (size_t j = 0; j < length / 2; ++j) {
        const auto a = data[start + j], b = data[start + j + length / 2] * phase;
        data[start + j] = a + b; data[start + j + length / 2] = a - b; phase *= step;
      }
    }
  }
}
}
struct AudioAnalysis::Impl {
  unsigned rate, channels, fft_size;
  bool force_mono, full_meter;
  size_t position = 0, interval_frames = 0, step_frames = 0, step_position = 0, rms_position = 0;
  uint64_t processed_frames = 0;
  std::vector<float> history, sample_peak, true_peak;
  std::vector<double> square;
  std::vector<uint64_t> clips;
  std::array<double, 4> rms_blocks{};
  double step_square = 0;
  double rms_m = -INFINITY, max_rms_m = -INFINITY;
  double lufs_momentary = -INFINITY, lufs_short_term = -INFINITY, max_lufs_m = -INFINITY, max_lufs_s = -INFINITY;
  ebur128_state* loudness = nullptr;
  std::unique_ptr<CockosMeter> cockos;
  ~Impl() { if (loudness) ebur128_destroy(&loudness); }
};
AudioAnalysis::AudioAnalysis(unsigned rate, unsigned channels, unsigned fft_size, bool force_mono, bool full_meter) : impl_(std::make_unique<Impl>()) {
  if (!rate || !channels || channels > 32 || fft_size < 32 || fft_size > 32768 || (fft_size & (fft_size - 1)))
    throw std::invalid_argument("Invalid audio analysis dimensions");
  auto& state = *impl_; state.rate = rate; state.channels = channels; state.fft_size = fft_size;
  state.force_mono = force_mono; state.full_meter = full_meter; state.step_frames = std::max(1u, (rate + 5) / 10);
  state.history.resize(fft_size * channels); state.sample_peak.resize(channels); state.square.resize(channels);
  state.true_peak.resize(channels); state.clips.resize(channels);
  state.loudness = ebur128_init(channels, rate, full_meter ? EBUR128_MODE_SAMPLE_PEAK :
    EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_HISTOGRAM);
  if (!state.loudness) throw std::runtime_error("Cannot allocate loudness analyzer");
  if (full_meter) state.cockos = std::make_unique<CockosMeter>(rate, channels);

}
AudioAnalysis::~AudioAnalysis() = default;
void AudioAnalysis::reset() {
  AudioAnalysis fresh(impl_->rate, impl_->channels, impl_->fft_size, impl_->force_mono, impl_->full_meter);
  impl_.swap(fresh.impl_);
}
void AudioAnalysis::process(const float* samples, size_t frames, bool integrate) {
  auto& state = *impl_;
  while (frames) {
    const size_t count = state.full_meter ? std::min(frames, state.step_frames - state.step_position) : frames;
    for (size_t i = 0; i < count; ++i) {
      for (unsigned channel = 0; channel < state.channels; ++channel) {
        const auto sample = samples[i * state.channels + channel];
        if (!std::isfinite(sample)) throw std::invalid_argument("Non-finite audio sample");
        state.history[state.position * state.channels + channel] = sample;
        if (!state.full_meter) state.sample_peak[channel] = std::max(state.sample_peak[channel], std::abs(sample));
        state.square[channel] += double(sample) * sample;
        if (state.full_meter) {
          state.step_square += double(sample) * sample;
          state.true_peak[channel] = std::max(state.true_peak[channel], static_cast<float>(state.cockos->sample(channel, sample)));
          if (std::abs(sample) > 1 && state.clips[channel] != UINT64_MAX) ++state.clips[channel];
        }
      }
      state.position = (state.position + 1) % state.fft_size;
    }
    state.processed_frames += count; state.interval_frames += count;
    if (ebur128_add_frames_float(state.loudness, samples, count) != EBUR128_SUCCESS) throw std::runtime_error("Loudness analysis failed");
    if (state.full_meter) {
      for (unsigned channel = 0; channel < state.channels; ++channel) {
        double peak = 0; ebur128_prev_sample_peak(state.loudness, channel, &peak);
        state.sample_peak[channel] = std::max(state.sample_peak[channel], static_cast<float>(peak));
      }
      state.step_position += count;
      if (state.step_position == state.step_frames) {
        state.rms_blocks[state.rms_position] = state.step_square;
        state.rms_position = (state.rms_position + 1) % state.rms_blocks.size();
        double sum = 0; for (const auto value : state.rms_blocks) sum += value;
        state.cockos->step(sum / (4 * state.step_frames), integrate);
        if (state.processed_frames >= 4 * state.step_frames) {
          state.rms_m = sum > 0 ? 10 * std::log10(sum / (4 * state.step_frames)) : -INFINITY;
          state.max_rms_m = std::max(state.max_rms_m, state.rms_m);
          state.lufs_momentary = state.cockos->lufs_momentary();
          state.max_lufs_m = std::max(state.max_lufs_m, state.lufs_momentary);
        }
        if (state.processed_frames >= 30 * state.step_frames) {
          state.lufs_short_term = state.cockos->lufs_short_term();
          state.max_lufs_s = std::max(state.max_lufs_s, state.lufs_short_term);
        }
        state.step_square = 0; state.step_position = 0;
      }
    }
    samples += count * state.channels; frames -= count;
  }
}
std::vector<float> AudioAnalysis::meter() {
  auto& state = *impl_; std::vector<float> result(meter_size(state.channels));
  const size_t base = ChannelCount * state.channels;
  const auto counter = [&](unsigned group, unsigned channel, uint64_t value) {
    const size_t at = base + GlobalCount + group * 3 * state.channels + channel;
    result[at] = static_cast<float>(value & 0xffffff);
    result[at + state.channels] = static_cast<float>((value >> 24) & 0xffffff);
    result[at + 2 * state.channels] = static_cast<float>(value >> 48);
  };
  for (unsigned channel = 0; channel < state.channels; ++channel) {
    result[channel] = state.sample_peak[channel];
    result[state.channels + channel] = state.true_peak[channel];
    result[2 * state.channels + channel] = state.interval_frames ? static_cast<float>(std::sqrt(state.square[channel] / state.interval_frames)) : 0;
    result[3 * state.channels + channel] = static_cast<float>(state.clips[channel]);
    counter(0, channel, state.clips[channel]);
    if (state.full_meter) {
      double peak = 0; ebur128_sample_peak(state.loudness, channel, &peak);
      result[ChannelMaxSamplePeak * state.channels + channel] = static_cast<float>(peak);
      result[base + MaxSamplePeak] = std::max(result[base + MaxSamplePeak], static_cast<float>(peak));
      peak = state.cockos->true_peak(channel);
      result[ChannelMaxTruePeak * state.channels + channel] = static_cast<float>(peak);
      result[base + MaxTruePeak] = std::max(result[base + MaxTruePeak], static_cast<float>(peak));
      result[TruePeakClipCount * state.channels + channel] = static_cast<float>(state.cockos->true_clips(channel));
      counter(1, channel, state.cockos->true_clips(channel));
    }
  }
  double lufs_integrated = -INFINITY;
  if (state.cockos) lufs_integrated = state.cockos->lufs_integrated();
  else ebur128_loudness_global(state.loudness, &lufs_integrated);
  const double mono = state.force_mono ? -3 : 0;
  result[base + RmsMomentary] = static_cast<float>(state.rms_m + mono);
  result[base + RmsIntegrated] = static_cast<float>((state.cockos ? state.cockos->rms_integrated() : -INFINITY) + mono);
  result[base + MaxRmsMomentary] = static_cast<float>(state.max_rms_m + mono);
  result[base + LufsMomentary] = static_cast<float>(state.lufs_momentary + mono);
  result[base + LufsShortTerm] = static_cast<float>(state.lufs_short_term + mono);
  result[base + LufsIntegrated] = static_cast<float>(lufs_integrated + mono);
  result[base + LoudnessRangeLow] = static_cast<float>((state.cockos ? state.cockos->range_low() : -100) + mono);
  result[base + LoudnessRangeHigh] = static_cast<float>((state.cockos ? state.cockos->range_high() : -100) + mono);
  result[base + LoudnessRange] = result[base + LoudnessRangeHigh] - result[base + LoudnessRangeLow];
  result[base + MaxLufsMomentary] = static_cast<float>(state.max_lufs_m + mono);
  result[base + MaxLufsShortTerm] = static_cast<float>(state.max_lufs_s + mono);
  result[base + ProcessedSeconds] = static_cast<float>(double(state.processed_frames) / state.rate);
  std::fill(state.sample_peak.begin(), state.sample_peak.end(), 0); std::fill(state.square.begin(), state.square.end(), 0); state.interval_frames = 0;
  std::fill(state.true_peak.begin(), state.true_peak.end(), 0);
  return result;
}
std::vector<float> AudioAnalysis::spectrum() const {
  const auto& state = *impl_; const auto bins = state.fft_size / 2 + 1;
  std::vector<float> result(bins * state.channels); std::vector<std::complex<double>> values(state.fft_size);
  for (unsigned channel = 0; channel < state.channels; ++channel) {
    for (unsigned i = 0; i < state.fft_size; ++i)
      values[i] = state.history[((state.position + i) % state.fft_size) * state.channels + channel] * (0.5 - 0.5 * std::cos(2 * pi * i / state.fft_size));
    fft(values);
    for (unsigned i = 0; i < bins; ++i)
      result[i * state.channels + channel] = static_cast<float>(std::abs(values[i]) * ((i == 0 || i == bins - 1) ? 2 : 4) / state.fft_size);
  }
  return result;
}
std::vector<float> AudioAnalysis::waveform(unsigned points) const {
  const auto& state = *impl_; points = std::max(1u, std::min(points, state.fft_size));
  std::vector<float> result(points * state.channels * 2);
  for (unsigned point = 0; point < points; ++point) for (unsigned channel = 0; channel < state.channels; ++channel) {
    float low = INFINITY, high = -INFINITY;
    for (unsigned i = point * state.fft_size / points; i < (point + 1) * state.fft_size / points; ++i) {
      const auto sample = state.history[((state.position + i) % state.fft_size) * state.channels + channel];
      low = std::min(low, sample); high = std::max(high, sample);
    }
    result[(point * state.channels + channel) * 2] = low; result[(point * state.channels + channel) * 2 + 1] = high;
  }
  return result;
}
}
