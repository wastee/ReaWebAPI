#include "runtime/aggregate_source.hpp"
#include "runtime/audio_analysis.hpp"
#include "runtime/native_producers.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <thread>
#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (0)
using namespace reaweb;
namespace {
struct Track;
struct Receive { Track* source; bool muted = false; int channel = 0; };
struct Track {
  unsigned char id;
  double amplitude;
  Track* parent = nullptr;
  bool main = true, mute = false, defeat = false, item_muted = false;
  int solo = 0, reads = 0;
  std::vector<Receive> receives;
};
std::vector<Track*> tracks;
std::set<void*> accessors;
const auto main_thread = std::this_thread::get_id();
int creates = 0, destroys = 0, rate_expected = 48000, channels_expected = 2, frames_expected = 2048;
double at_expected = 1.25;
bool fail_create = false, fail_read = false, no_audio = false;
int sample_delay = 0;
void* project = reinterpret_cast<void*>(1);
Guid guid(void* track) { Guid result{}; result[0] = static_cast<Track*>(track)->id; return result; }
double value(void* pointer, const char* key) {
  const auto& t = *static_cast<Track*>(pointer);
  if (!std::strcmp(key, "B_MAINSEND")) return t.main;
  if (!std::strcmp(key, "B_MUTE")) return t.mute;
  if (!std::strcmp(key, "I_SOLO")) return t.solo;
  if (!std::strcmp(key, "B_SOLO_DEFEAT")) return t.defeat;
  throw std::runtime_error(key);
}
Host host() {
  Host h;
  h.current_project = [] { return project; };
  h.track_count = [] { return static_cast<int>(tracks.size()); };
  h.track_guid = guid;
  h.native_function = [](const char* name) -> void* {
    CHECK(std::this_thread::get_id() == main_thread);
    if (!std::strcmp(name, "GetTrack")) return reinterpret_cast<void*>(+[](void*, int index) -> void* { return tracks.at(index); });
    if (!std::strcmp(name, "GetParentTrack")) return reinterpret_cast<void*>(+[](void* t) -> void* { return static_cast<Track*>(t)->parent; });
    if (!std::strcmp(name, "GetMediaTrackInfo_Value")) return reinterpret_cast<void*>(value);
    if (!std::strcmp(name, "GetTrackNumSends")) return reinterpret_cast<void*>(+[](void* t, int category) { CHECK(category == -1); return static_cast<int>(static_cast<Track*>(t)->receives.size()); });
    if (!std::strcmp(name, "GetSetTrackSendInfo")) return reinterpret_cast<void*>(+[](void* t, int c, int i, const char* key, void* write) -> void* {
      CHECK(c == -1 && !write && !std::strcmp(key, "P_SRCTRACK")); return static_cast<Track*>(t)->receives.at(i).source;
    });
    if (!std::strcmp(name, "GetTrackSendInfo_Value")) return reinterpret_cast<void*>(+[](void* t, int c, int i, const char* key) -> double {
      CHECK(c == -1); const auto& r = static_cast<Track*>(t)->receives.at(i);
      if (!std::strcmp(key, "B_MUTE")) return r.muted;
      CHECK(!std::strcmp(key, "I_SRCCHAN")); return r.channel;
    });
    if (!std::strcmp(name, "CreateTrackAudioAccessor")) return reinterpret_cast<void*>(+[](void* t) -> void* {
      CHECK(std::this_thread::get_id() == main_thread);
      if (fail_create) return nullptr;
      auto* accessor = new Track*(static_cast<Track*>(t)); accessors.insert(accessor); ++creates; return accessor;
    });
    if (!std::strcmp(name, "DestroyAudioAccessor")) return reinterpret_cast<void*>(+[](void* a) {
      CHECK(std::this_thread::get_id() == main_thread && accessors.erase(a) == 1); delete static_cast<Track**>(a); ++destroys;
    });
    if (!std::strcmp(name, "AudioAccessorValidateState")) return reinterpret_cast<void*>(+[](void* a) { CHECK(accessors.count(a)); return true; });
    if (!std::strcmp(name, "GetAudioAccessorSamples")) return reinterpret_cast<void*>(+[](void* a, int rate, int channels, double at, int frames, double* output) {
      CHECK(std::this_thread::get_id() == main_thread && accessors.count(a));
      CHECK(rate == rate_expected && channels == channels_expected && at == at_expected && frames == frames_expected);
      auto& t = **static_cast<Track**>(a); ++t.reads;
      if (sample_delay) std::this_thread::sleep_for(std::chrono::milliseconds(sample_delay));
      if (fail_read) return -1;
      if (no_audio) { output[0] = 99; return 0; }
      for (int i = 0; i < frames; ++i) for (int ch = 0; ch < channels; ++ch)
        output[i * channels + ch] = t.item_muted ? 0 : t.amplitude * std::sin(2 * 3.141592653589793 * i / 32);
      return 1;
    });
    throw std::runtime_error(std::string("Unexpected host call: ") + name);
  };
  return h;
}
std::vector<float> read(AggregateSource& source, double amplitude) {
  std::vector<double> output(frames_expected * channels_expected, 123);
  CHECK(source.read(rate_expected, channels_expected, at_expected, frames_expected, output.data()) == 1);
  for (int i = 0; i < frames_expected; ++i) for (int ch = 0; ch < channels_expected; ++ch)
    CHECK(std::abs(output[i * channels_expected + ch] - amplitude * std::sin(2 * 3.141592653589793 * i / 32)) < 1e-12);
  std::vector<float> pcm(output.size());
  std::transform(output.begin(), output.end(), pcm.begin(), [](double value) { return static_cast<float>(value); });
  return pcm;
}
}
int main() {
  try {
    auto h = host();
    Track root{1, .75}, child{2, .75}, nested{3, .25}, leaf{4, .5}, bus{5, 0}, other{6, 4};
    tracks = {&root, &child, &nested, &leaf, &bus, &other};
    {
      AggregateSource source(h, &root);
      read(source, .75); const int initial = creates;
      read(source, .75); CHECK(creates == initial);
      child.parent = &root; nested.parent = &root; leaf.parent = &nested;
      auto pcm = read(source, 2.25); CHECK(root.reads == 3 && child.reads == 1 && leaf.reads == 1);
      AudioAnalysis analysis(48000, 2, 2048);
      for (int i = 0; i < 100; ++i) analysis.process(pcm.data(), 2048);
      CHECK(std::abs(analysis.spectrum()[64 * 2] - 2.25f) < .001f);
      const auto meter = analysis.meter(); CHECK(meter[0] > 2.24 && std::abs(meter[2] - 2.25 / std::sqrt(2.)) < .001);
      CHECK(std::isfinite(meter[4]) && std::isfinite(meter[5]) && std::isfinite(meter[6]));
      const auto wave = analysis.waveform(256); CHECK(*std::max_element(wave.begin(), wave.end()) > 2.24);
      root.receives = {{&child}, {&leaf}, {&bus}}; bus.receives = {{&root}};
      const int before = child.reads; read(source, 2.25); CHECK(child.reads == before + 1);
      child.mute = true; read(source, 1.5); child.mute = false;
      nested.mute = true; read(source, 1.5); nested.mute = false;
      root.mute = true; read(source, 0); CHECK(accessors.empty()); root.mute = false;
      child.item_muted = true; read(source, 1.5); child.item_muted = false;
      root.receives.clear(); bus.receives.clear();
      child.main = false; read(source, 1.5); child.main = true;
      other.solo = 2; read(source, 0);
      child.defeat = true; read(source, .75); child.defeat = false; other.solo = 0;
      child.solo = 2; read(source, .75); child.solo = 0;
      nested.solo = 1; read(source, .75); nested.solo = 0;
      root.solo = 2; read(source, 2.25); root.solo = 0;
      root.amplitude = -.75; nested.amplitude = -.5; read(source, 0);
      root.amplitude = .75; nested.amplitude = .25;
      // Changes to the track list and reused pointers must retire old accessors.
      const int old_creates = creates, old_destroys = destroys;
      child.id = 42; read(source, 2.25); CHECK(creates == old_creates + 1 && destroys == old_destroys + 1);
      tracks.erase(tracks.begin() + 1); read(source, 1.5);
      no_audio = true; read(source, 0); no_audio = false;
      double output[4096]; fail_read = true; CHECK(source.read(48000, 2, at_expected, 2048, output) < 0); fail_read = false;
      project = reinterpret_cast<void*>(2); CHECK(source.read(48000, 2, at_expected, 2048, output) < 0); project = reinterpret_cast<void*>(1);
      root.id = 43; CHECK(source.read(48000, 2, at_expected, 2048, output) < 0);
    }
    CHECK(accessors.empty() && creates == destroys);
    tracks = {&root, &child, &nested, &leaf, &bus, &other};
    child.parent = nested.parent = leaf.parent = nullptr;
    bus.receives = {{&root}, {&child}, {&nested, true}, {&leaf, false, -1}};
    root.receives = {{&leaf}};
    {
      AggregateSource source(h, &bus);
      read(source, 2.0);
      bus.solo = 1; read(source, 2.0); bus.solo = 0;
      child.solo = 1; read(source, 0); child.solo = 2; read(source, .75); child.solo = 0;
      root.solo = 1; child.solo = 2; read(source, .75); root.solo = child.solo = 0;
      bus.receives[0].muted = true; read(source, .75);
      channels_expected = 1; frames_expected = 128; rate_expected = 44100; at_expected = 7;
      read(source, .75);
    }
    CHECK(accessors.empty() && creates == destroys);
    {
      AggregateSource source(h, &bus); fail_create = true;
      double output[128]; CHECK(source.read(44100, 1, 7, 128, output) < 0); fail_create = false;
    }
    CHECK(accessors.empty() && creates == destroys);
    {
      root.id = 1; root.mute = false; root.receives.clear();
      child.parent = &root; tracks = {&root, &child};
      int revision = 0; h.change_count = [&](void*) { return revision; };
      AggregateSource source(h, &root);
      double output[128]; std::fill(std::begin(output), std::end(output), 123);
      sample_delay = 10;
      auto step = [&] { return source.read(44100, 1, 7, 128, output, std::chrono::steady_clock::now() + std::chrono::milliseconds(2)); };
      CHECK(step() == 2 && output[0] == 123);
      CHECK(step() == 1);
      CHECK(std::abs(output[8] - 1.5) < 1e-12);
      CHECK(step() == 2);
      child.mute = true; ++revision;
      CHECK(step() == 1 && std::abs(output[8] - .75) < 1e-12);
      child.mute = false; ++revision;
      CHECK(step() == 2);
      tracks = {&child}; ++revision;
      CHECK(step() == -1);
      sample_delay = 0;
    }
    CHECK(accessors.empty() && creates == destroys);
    {
      h.change_count = [](void*) { return 0; };
      const auto native = h.native_function;
      h.native_function = [native](const char* name) -> void* {
        if (!std::strcmp(name, "GetAudioDeviceInfo")) return reinterpret_cast<void*>(+[](const char*, char* out, int size) { std::snprintf(out, size, "48000"); return true; });
        if (!std::strcmp(name, "GetSelectedTrack")) return reinterpret_cast<void*>(+[](void*, int) -> void* { return tracks.front(); });
        if (!std::strcmp(name, "ValidatePtr2")) return reinterpret_cast<void*>(+[](void*, void* t, const char*) { return std::find(tracks.begin(), tracks.end(), t) != tracks.end(); });
        if (!std::strcmp(name, "GetPlayState")) return reinterpret_cast<void*>(+[] { return 0; });
        if (!std::strcmp(name, "GetPlayPosition") || !std::strcmp(name, "GetCursorPosition")) return reinterpret_cast<void*>(+[] { return at_expected; });
        return native(name);
      };
      rate_expected = 48000; channels_expected = 2; frames_expected = 1600;
      tracks = {&root, &child}; child.parent = &root;
      StreamHub hub; NativeProducers producers(h, hub);
      for (const bool aggregate : {false, true}) {
        const int before = creates, reads = root.reads;
        std::vector<std::string> names;
        for (const auto* kind : {"meter", "spectrum", "waveform"}) {
          names.push_back(producers.audio(kind, Json{{"source", "selected-track"}, {"aggregate", aggregate}}, 1));
          hub.attach(names.back(), 1, 0, "http://127.0.0.1:1234"); producers.attached(names.back());
        }
        sample_delay = aggregate ? 10 : 0;
        for (int i = 0; i < 200; ++i) {
          producers.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
          const auto state = hub.info(); bool complete = true;
          for (const auto& stream : state["streams"]) complete = complete && stream["published"].get<int>() > 0;
          if (complete) break;
        }
        const auto final_state = hub.info();
        for (const auto& stream : final_state["streams"]) CHECK(stream["published"].get<int>() > 0);
        CHECK(creates == before + (aggregate ? 2 : 1));
        CHECK(root.reads == reads + 1);
        sample_delay = 0;
        hub.detach_window(1); producers.tick(); CHECK(hub.info()["streams"].empty());
        CHECK(accessors.empty() && creates == destroys);
      }
    }
    std::cout << "Aggregate PCM, folders, receives, mute/solo, cycles, identity, analysis and accessor lifetime passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
