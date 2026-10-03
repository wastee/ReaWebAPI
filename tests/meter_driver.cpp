#include "runtime/native_producers.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace reaweb;
namespace {
double position = 0, end_position = 0;
int playing = 0, rate = 48000, revision = 0, reads = 0;
int track_channels = 2, input_channels = 2, output_channels = 2;
bool refreshed = false;
double tone[32][8192];
int track;
void* native(const char* name) {
  if (!std::strcmp(name, "GetNumAudioInputs")) return reinterpret_cast<void*>(+[] { return input_channels; });
  if (!std::strcmp(name, "GetNumAudioOutputs")) return reinterpret_cast<void*>(+[] { return output_channels; });
  if (!std::strcmp(name, "GetAudioDeviceInfo")) return reinterpret_cast<void*>(+[](const char*, char* out, int size) { std::snprintf(out, size, "%d", rate); return true; });
  if (!std::strcmp(name, "GetSelectedTrack") || !std::strcmp(name, "GetTrack")) return reinterpret_cast<void*>(+[](void*, int) -> void* { return &track; });
  if (!std::strcmp(name, "ValidatePtr2")) return reinterpret_cast<void*>(+[](void*, void*, const char*) { return true; });
  if (!std::strcmp(name, "GetPlayState")) return reinterpret_cast<void*>(+[] { return playing; });
  if (!std::strcmp(name, "GetPlayPosition") || !std::strcmp(name, "GetCursorPosition")) return reinterpret_cast<void*>(+[] { return position; });
  if (!std::strcmp(name, "CreateTrackAudioAccessor")) return reinterpret_cast<void*>(+[](void* t) { return t; });
  if (!std::strcmp(name, "DestroyAudioAccessor")) return reinterpret_cast<void*>(+[](void*) {});
  if (!std::strcmp(name, "AudioAccessorValidateState")) return reinterpret_cast<void*>(+[](void*) { const bool value = refreshed; refreshed = false; return value; });
  if (!std::strcmp(name, "GetAudioAccessorSamples")) return reinterpret_cast<void*>(+[](void*, int sr, int channels, double at, int frames, double* out) {
    ++reads; end_position = at + double(frames) / sr;
    for (int i = 0; i < frames; ++i) for (int ch = 0; ch < channels; ++ch) out[i * channels + ch] = .5 * std::sin(2 * 3.141592653589793 * 1000 * (at + double(i) / sr));
    return 1;
  });
  if (!std::strcmp(name, "GetParentTrack")) return reinterpret_cast<void*>(+[](void*) -> void* { return nullptr; });
  if (!std::strcmp(name, "GetMediaTrackInfo_Value")) return reinterpret_cast<void*>(+[](void*, const char* key) { return !std::strcmp(key, "I_NCHAN") ? double(track_channels) : 0.; });
  if (!std::strcmp(name, "GetTrackNumSends")) return reinterpret_cast<void*>(+[](void*, int) { return 0; });
  if (!std::strcmp(name, "GetSetTrackSendInfo")) return reinterpret_cast<void*>(+[](void*, int, int, const char*, void*) -> void* { return nullptr; });
  if (!std::strcmp(name, "GetTrackSendInfo_Value")) return reinterpret_cast<void*>(+[](void*, int, int, const char*) { return 0.; });
  return nullptr;
}
}
int main() {
  Host host; host.native_function = native; host.current_project = []() -> void* { return &track; };
  host.change_count = [](void*) { return revision; }; host.track_count = [] { return 1; };
  host.track_identity = [](int) { return "test"; }; host.track_guid = [](void*) { return Guid{}; };
  StreamHub hub; NativeProducers producers(host, hub);
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      const auto request = Json::parse(line); const auto command = request.at("command").get<std::string>(); Json result = true;
      if (command == "configure") {
        track_channels = request.value("trackChannels", track_channels);
        input_channels = request.value("inputChannels", input_channels); output_channels = request.value("outputChannels", output_channels);
        producers.capture(false, 0, rate, input_channels, nullptr); producers.capture(true, 0, rate, output_channels, nullptr);
      } else if (command == "open") {
        const auto name = producers.audio(request.value("kind", "meter"), request.value("options", Json::object()), 1);
        result = hub.attach(name, 1, 0, "http://127.0.0.1:9000"); producers.attached(name);
      } else if (command == "reset") producers.reset_meter(request.at("name"), request.value("window", 1));
      else if (command == "tick") {
        track_channels = request.value("trackChannels", track_channels);
        position = request.value("position", position); playing = request.value("playing", playing);
        revision += request.value("revision", 0); refreshed = request.value("refreshed", false);
        producers.tick(); result = {{"reads", reads}, {"end", end_position}};
      } else if (command == "capture") {
        rate = request.value("rate", rate); const int frames = request.value("frames", 4800);
        const double amplitude = request.value("amplitude", .5);
        const bool output = request.value("output", true);
        int& channels = output ? output_channels : input_channels; channels = request.value("channels", channels);
        for (int ch = 0; ch < 32; ++ch) for (int i = 0; i < frames && i < 8192; ++i)
          tone[ch][i] = amplitude * (request.value("channelRamp", false) ? ch + 1 : 1) * std::sin(2 * 3.141592653589793 * 1000 * i / rate);
        producers.capture(output, frames, rate, channels, +[](bool, int ch) { return tone[ch]; });
      } else if (command == "close") { hub.detach_window(1); producers.tick(); }
      else if (command == "info") result = hub.info();
      else throw std::runtime_error("Unknown test command");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      std::cout << Json{{"result", result}}.dump() << std::endl;
    } catch (const Error& error) { std::cout << Json{{"error", error.code}}.dump() << std::endl; }
      catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
  }
}
