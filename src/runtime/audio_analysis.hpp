#pragma once
#include <cstddef>
#include <memory>
#include <vector>
namespace reaweb {
/* Worker-only DSP. PCM ingestion into its input queue is separate and realtime safe. */
class AudioAnalysis {
public:
  enum MeterChannel { SamplePeak, TruePeak, ChannelRms, SampleClipCount, TruePeakClipCount,
    ChannelMaxSamplePeak, ChannelMaxTruePeak, ChannelCount };
  // Built-in meter: seven channel arrays, fourteen globals, then six exact-count limb arrays.
  enum MeterGlobal { RmsMomentary, RmsIntegrated, MaxRmsMomentary,
    LufsMomentary, LufsShortTerm, LufsIntegrated, LoudnessRange,
    MaxLufsMomentary, MaxLufsShortTerm, MaxSamplePeak, MaxTruePeak, ProcessedSeconds,
    LoudnessRangeLow, LoudnessRangeHigh, GlobalCount };
  AudioAnalysis(unsigned rate, unsigned channels, unsigned fft_size, bool force_mono = false, bool full_meter = true);
  ~AudioAnalysis();
  void reset();
  void process(const float* interleaved, size_t frames, bool integrate = true);
  static size_t meter_size(unsigned channels) { return 13 * channels + GlobalCount; }
  std::vector<float> meter();
  std::vector<float> spectrum() const;
  std::vector<float> waveform(unsigned points) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
