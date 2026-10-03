#include "runtime/audio_analysis.hpp"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (0)
using reaweb::AudioAnalysis;
namespace {
constexpr double pi = 3.14159265358979323846;
void near(double a, double b, double tolerance = 1e-4) { CHECK(a == b || std::abs(a - b) <= tolerance); }
std::vector<float> signal(unsigned rate, unsigned channels, unsigned seconds, double amplitude = .5, double frequency = 1000) {
  std::vector<float> pcm(size_t(rate) * channels * seconds);
  for (size_t i = 0; i < pcm.size() / channels; ++i) for (unsigned ch = 0; ch < channels; ++ch)
    pcm[i * channels + ch] = static_cast<float>(amplitude * std::sin(2 * pi * frequency * i / rate));
  return pcm;
}
std::vector<float> analyze(const std::vector<float>& pcm, unsigned rate, unsigned channels, unsigned update, bool mono = false) {
  AudioAnalysis analyzer(rate, channels, 2048, mono);
  size_t position = 0, publication = 0;
  // Irregular ingestion boundaries and publication boundaries exercise true-peak filter continuity.
  while (position < pcm.size() / channels) {
    const size_t count = std::min<size_t>(137 + (position + update * 53) % 791, pcm.size() / channels - position);
    analyzer.process(pcm.data() + position * channels, count); position += count;
    if (position * update / rate != publication && position != pcm.size() / channels) {
      publication = position * update / rate; analyzer.meter();
    }
  }
  return analyzer.meter();
}
void rates_and_signals() {
  for (unsigned rate : {44100, 48000, 96000}) for (unsigned channels : {1, 2, 6}) {
    const auto pcm = signal(rate, channels, 4);
    const size_t base = AudioAnalysis::ChannelCount * channels;
    auto reference = analyze(pcm, rate, channels, 10);
    near(reference[base + AudioAnalysis::RmsMomentary], 10 * std::log10(channels * .125));
    near(reference[base + AudioAnalysis::RmsIntegrated], 10 * std::log10(channels * .125 * (40 - 1.5) / 40));
    near(reference[base + AudioAnalysis::ProcessedSeconds], 4);
    CHECK(reference[base + AudioAnalysis::MaxTruePeak] >= reference[base + AudioAnalysis::MaxSamplePeak]);
    const double loudness = -9.024 + 10 * std::log10(channels == 6 ? 3 + 2 * 2 : channels);
    near(reference[base + AudioAnalysis::LufsIntegrated], loudness, .15);
    near(reference[base + AudioAnalysis::LoudnessRange], 0, .1);
    for (unsigned update : {30, 60}) {
      const auto meter = analyze(pcm, rate, channels, update);
      for (size_t i = 3 * channels; i < meter.size(); ++i) near(reference[i], meter[i]);
    }
    const auto mono = analyze(pcm, rate, channels, 30, true);
    for (unsigned i : {0, 1, 2, 3, 4, 5, 7, 8}) near(mono[base + i], reference[base + i] - 3);
    near(mono[base + AudioAnalysis::MaxTruePeak], reference[base + AudioAnalysis::MaxTruePeak]);
    near(mono[base + AudioAnalysis::LoudnessRange], reference[base + AudioAnalysis::LoudnessRange]);
    for (const double amplitude : {0.0, .999, 1.5}) {
      AudioAnalysis analyzer(rate, channels, 2048);
      auto input = signal(rate, channels, 1, amplitude);
      analyzer.process(input.data(), input.size() / channels); const auto m = analyzer.meter();
      for (unsigned ch = 0; ch < channels; ++ch) {
        uint64_t clips = 0; for (size_t i = ch; i < input.size(); i += channels) if (std::abs(input[i]) > 1) ++clips;
        near(m[3 * channels + ch], double(clips));
        near(m[2 * channels + ch], amplitude / std::sqrt(2.0));
      }
      CHECK(!std::isnan(m[base + AudioAnalysis::LufsIntegrated]));
      if (!amplitude) CHECK(m[base + AudioAnalysis::RmsIntegrated] == -INFINITY && m[base + AudioAnalysis::LufsIntegrated] == -INFINITY);
      analyzer.reset(); const auto empty = analyzer.meter();
      CHECK(empty[base + AudioAnalysis::ProcessedSeconds] == 0 && empty[base + AudioAnalysis::MaxSamplePeak] == 0);
      CHECK(empty[base + AudioAnalysis::RmsIntegrated] == -INFINITY && empty[3 * channels] == 0);
    }
    AudioAnalysis impulse(rate, channels, 2048);
    std::vector<float> input(rate * channels, 0); input[32 * channels] = 1;
    impulse.process(input.data(), rate); const auto m = impulse.meter();
    near(m[base + AudioAnalysis::MaxSamplePeak], 1); near(m[base + AudioAnalysis::RmsIntegrated], -10 * std::log10(rate));
    CHECK(m[3 * channels] == 0);
  }
  for (unsigned rate : {44100, 48000, 96000}) {
    auto pcm = signal(rate, 2, 1, 1.1, rate / 4.0);
    // Quarter-cycle offset creates an inter-sample peak above 0 dBFS with all samples below it.
    for (size_t i = 0; i < pcm.size() / 2; ++i) pcm[2 * i] = pcm[2 * i + 1] = float(1.1 * std::sin(pi * i / 2 + pi / 4));
    const auto m = analyze(pcm, rate, 2, 60);
    CHECK(m[14 + AudioAnalysis::MaxSamplePeak] < 1 && m[14 + AudioAnalysis::MaxTruePeak] > 1.05 && m[6] == 0);
    CHECK(m[7] == 0 && m[8] > 0 && m[8] == m[9]);
  }
}
void history() {
  const unsigned rate = 48000;
  auto pcm = signal(rate, 2, 1, .1);
  AudioAnalysis analyzer(rate, 2, 2048);
  for (int second = 0; second < 180; ++second) {
    const float scale = second < 90 ? 1 : 4;
    auto block = pcm; for (auto& value : block) value *= scale;
    analyzer.process(block.data(), rate); if (second % 3 == 0) analyzer.meter();
  }
  const auto m = analyzer.meter();
  near(m[14 + AudioAnalysis::RmsIntegrated], 10 * std::log10((.01 * (900 - 1.5) + .16 * 900 - (.16 - .01) * 1.5) / 1800), 1e-4);
  near(m[14 + AudioAnalysis::ProcessedSeconds], 180);
  near(m[14 + AudioAnalysis::LoudnessRange], 20 * std::log10(4.), .2);
  near(m[14 + AudioAnalysis::MaxRmsMomentary], 20 * std::log10(.4), 1e-4);
  CHECK(m[14 + AudioAnalysis::LufsIntegrated] > -13 && m[14 + AudioAnalysis::LufsIntegrated] < -7);
  // Deterministic pink noise with level changes exercises gating and non-periodic true peaks.
  uint32_t random = 17; double b0 = 0, b1 = 0, b2 = 0;
  auto noise = signal(rate, 2, 12, 0);
  for (size_t i = 0; i < noise.size() / 2; ++i) {
    random = random * 1664525 + 1013904223; const double white = double(random) / UINT32_MAX * 2 - 1;
    b0 = .99765 * b0 + white * .0990460; b1 = .963 * b1 + white * .2965164; b2 = .57 * b2 + white * 1.0526913;
    noise[2 * i] = noise[2 * i + 1] = float((b0 + b1 + b2 + white * .1848) * (i < rate * 6 ? .02 : .1));
  }
  const auto a = analyze(noise, rate, 2, 10), b = analyze(noise, rate, 2, 60);
  for (size_t i = 6; i < a.size(); ++i) near(a[i], b[i]);
  CHECK(a[14 + AudioAnalysis::LoudnessRange] > 5);
  CHECK(a[14 + AudioAnalysis::LoudnessRange] == a[14 + AudioAnalysis::LoudnessRangeHigh] - a[14 + AudioAnalysis::LoudnessRangeLow]);
  auto many = signal(44100, 32, 1);
  CHECK(analyze(many, 44100, 32, 30).size() == 430);
}
void lifecycle() {
  const unsigned rate = 48000, channels = 2, base = AudioAnalysis::ChannelCount * channels;
  const auto loud = signal(rate, channels, 4, 1.5), quiet = signal(rate, channels, 1, .25);
  for (bool mono : {false, true}) {
    AudioAnalysis analyzer(rate, channels, 2048, mono), fresh(rate, channels, 2048, mono);
    analyzer.process(loud.data(), loud.size() / channels);
    const auto first = analyzer.meter(), repeated = analyzer.meter();
    near(first[0], 1.5); near(first[base + AudioAnalysis::MaxSamplePeak], 1.5);
    near(first[base + AudioAnalysis::ProcessedSeconds], 4);
    CHECK(std::isfinite(first[base + AudioAnalysis::LufsShortTerm]));
    for (unsigned i = 0; i < 2 * channels; ++i) CHECK(repeated[i] == 0);
    for (size_t i = base; i < first.size(); ++i) near(repeated[i], first[i]);
    analyzer.reset();
    CHECK(analyzer.meter() == fresh.meter());
    analyzer.reset();
    analyzer.process(quiet.data(), quiet.size() / channels);
    fresh.process(quiet.data(), quiet.size() / channels);
    const auto restarted = analyzer.meter(), expected = fresh.meter();
    CHECK(restarted == expected);
    near(restarted[base + AudioAnalysis::MaxSamplePeak], .25);
    near(restarted[base + AudioAnalysis::ProcessedSeconds], 1);
    CHECK(restarted[base + AudioAnalysis::LufsShortTerm] == -INFINITY);
  }
}
void peak_intervals() {
  for (unsigned rate : {44100, 48000, 96000, 192000}) for (unsigned channels : {1, 2, 6, 32}) {
    AudioAnalysis analyzer(rate, channels, 2048);
    const unsigned base = AudioAnalysis::ChannelCount * channels;
    float max_sample = 0, max_true = 0;
    std::vector<float> channel_sample(channels), channel_true(channels);
    for (unsigned interval = 0; interval < 3; ++interval) {
      if (interval == 2) {
        analyzer.reset(); max_sample = max_true = 0;
        std::fill(channel_sample.begin(), channel_sample.end(), 0);
        std::fill(channel_true.begin(), channel_true.end(), 0);
        const auto reset = analyzer.meter();
        for (unsigned i = 0; i < base; ++i) CHECK(reset[i] == 0);
      }
      const size_t frames = rate / 4 + 17;
      std::vector<float> pcm(frames * channels), peaks(channels);
      for (size_t i = 0; i < frames; ++i) for (unsigned ch = 0; ch < channels; ++ch)
        pcm[i * channels + ch] = float((ch + 1) * .05 / channels * std::sin(2 * pi * 997 * i / rate));
      if (!interval) {
        pcm[17 * channels] = -.75f;
        pcm[101 * channels + channels - 1] = 1.25f;
        pcm[(frames / 2) * channels + (channels >= 6 ? 3 : 0)] = -1.75f;
      }
      for (size_t i = 0; i < frames; ++i) for (unsigned ch = 0; ch < channels; ++ch)
        peaks[ch] = std::max(peaks[ch], std::abs(pcm[i * channels + ch]));
      for (size_t at = 0; at < frames;) {
        const auto count = std::min<size_t>(137 + at % 7193, frames - at);
        analyzer.process(pcm.data() + at * channels, count); at += count;
      }
      const auto values = analyzer.meter();
      for (unsigned ch = 0; ch < channels; ++ch) {
        CHECK(values[ch] == peaks[ch]);
        max_sample = std::max(max_sample, peaks[ch]);
        max_true = std::max(max_true, values[channels + ch]);
        channel_sample[ch] = std::max(channel_sample[ch], values[ch]);
        channel_true[ch] = std::max(channel_true[ch], values[channels + ch]);
        CHECK(values[5 * channels + ch] == channel_sample[ch]);
        CHECK(values[6 * channels + ch] == channel_true[ch]);
      }
      CHECK(values[base + AudioAnalysis::MaxSamplePeak] == max_sample);
      CHECK(values[base + AudioAnalysis::MaxTruePeak] == max_true);
      const auto repeated = analyzer.meter();
      for (unsigned i = 3 * channels; i < base; ++i) CHECK(repeated[i] == values[i]);
      for (unsigned ch = 0; ch < channels; ++ch) CHECK(repeated[ch] == 0);
      CHECK(repeated[base + AudioAnalysis::MaxSamplePeak] == max_sample);
      CHECK(repeated[base + AudioAnalysis::MaxTruePeak] == max_true);
    }
  }
}
void precise_counts() {
  AudioAnalysis analyzer(192000, 1, 32);
  const size_t frames = (size_t(1) << 24) + 1;
  std::vector<float> pcm(8192, 1.5f);
  for (size_t at = 0; at < frames;) {
    const auto count = std::min(pcm.size(), frames - at);
    analyzer.process(pcm.data(), count); at += count;
  }
  auto m = analyzer.meter();
  const auto exact = [&](const std::vector<float>& data, unsigned offset) {
    return uint64_t(data[offset]) | (uint64_t(data[offset + 1]) << 24) | (uint64_t(data[offset + 2]) << 48);
  };
  CHECK(exact(m, 21) == frames && uint64_t(m[3]) != frames);
  const auto true_before = exact(m, 24);
  analyzer.process(pcm.data(), 8192); m = analyzer.meter();
  CHECK(exact(m, 21) == frames + 8192 && exact(m, 24) == true_before + 8192);
  const auto held = analyzer.meter();
  CHECK(exact(held, 21) == exact(m, 21) && exact(held, 24) == exact(m, 24));
  analyzer.reset(); m = analyzer.meter(); CHECK(exact(m, 21) == 0 && exact(m, 24) == 0);
}

}
int main() {
  try {
    reaweb::AudioAnalysis analysis(48000, 2, 2048);
    float samples[2048 * 2];
    for (int frame = 0; frame < 2048; ++frame) {
      samples[frame * 2] = static_cast<float>(0.5 * std::sin(2 * 3.141592653589793 * frame * 64 / 2048));
      samples[frame * 2 + 1] = samples[frame * 2];
    }
    for (int i = 0; i < 100; ++i) analysis.process(samples, 2048);
    auto meter = analysis.meter();
    CHECK(meter.size() == 40 && std::abs(meter[0] - 0.5) < 0.001);
    CHECK(std::abs(meter[4] - 0.5 / std::sqrt(2.0)) < 0.001);
    CHECK(std::isfinite(meter[17]) && std::isfinite(meter[18]) && std::isfinite(meter[19]));
    CHECK(meter[17] > -15 && meter[17] < -3);
    auto spectrum = analysis.spectrum();
    CHECK(spectrum.size() == 1025 * 2);
    CHECK(std::abs(spectrum[64 * 2] - 0.5) < 0.001 && spectrum[10] < 0.001);
    auto waveform = analysis.waveform(128);
    CHECK(waveform.size() == 128 * 4);
    for (size_t i = 0; i < waveform.size(); i += 2) CHECK(waveform[i] <= waveform[i + 1]);
    auto reset_meter = analysis.meter(); CHECK(reset_meter[0] == 0 && reset_meter[4] == 0);
    rates_and_signals(); history(); lifecycle(); peak_intervals(); precise_counts();
    std::cout << "PCM peak/RMS/LUFS, FFT calibration and waveform envelopes passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
