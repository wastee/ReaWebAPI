#include "runtime/audio_analysis.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  const unsigned rate = static_cast<unsigned>(std::stoul(argv[2]));
  const unsigned channels = static_cast<unsigned>(std::stoul(argv[3]));
  size_t remaining = static_cast<size_t>(std::stoull(argv[4]));
  std::ifstream input(argv[1], std::ios::binary); input.seekg(44);
  reaweb::AudioAnalysis analyzer(rate, channels, 2048);
  std::vector<float> block(4093 * channels);
  while (remaining) {
    const auto frames = std::min<size_t>(4093, remaining);
    if (!input.read(reinterpret_cast<char*>(block.data()), frames * channels * sizeof(float))) return 3;
    analyzer.process(block.data(), frames); remaining -= frames;
  }
  std::cout << nlohmann::json(analyzer.meter()).dump() << '\n';
}
