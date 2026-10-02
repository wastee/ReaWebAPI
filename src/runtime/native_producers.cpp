#include "runtime/native_producers.hpp"
#include "runtime/audio_analysis.hpp"
#include "runtime/aggregate_source.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace reaweb {
namespace {
template<class F> F function(const Host& host, const char* name) {
  return reinterpret_cast<F>(host.native_function ? host.native_function(name) : nullptr);
}
ReaWeb_StreamDesc audio_desc(unsigned rate = 48000) {
  ReaWeb_StreamDesc desc{}; desc.size = sizeof(desc); desc.abi_version = 1; desc.format = REAWEB_FLOAT32;
  desc.channels = 2; desc.sample_rate = rate; desc.block_frames = 8192; desc.max_bytes = 8192 * 2 * 4; desc.capacity = 16;
  return desc;
}
}
struct NativeProducers::Impl {
  struct TrackSource {
    const Host& host;
    void* track;
    void* project;
    std::string identity;
    unsigned rate;
    double update_rate;
    void* accessor = nullptr;
    std::unique_ptr<AggregateSource> aggregate;
    uint64_t tick = 0, sequence = 0;
    int error = 0;
    bool pending = false;
    double position = 0, timestamp = 0;
    std::chrono::steady_clock::time_point next_sample{};
    std::array<double, 8192 * 2> samples{};
    std::array<float, 8192 * 2> floats{};
    TrackSource(const Host& h, void* t, std::string id, unsigned r, double update)
      : host(h), track(t), project(h.current_project ? h.current_project() : nullptr), identity(std::move(id)), rate(r), update_rate(update) {}
    ~TrackSource() { if (accessor) function<void (*)(void*)>(host, "DestroyAudioAccessor")(accessor); }
    int frames() const { return std::min(8192, std::max(1, static_cast<int>(rate / update_rate))); }
    void sample(uint64_t cycle, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point deadline) {
      if (tick == cycle || now < next_sample || error) return;
      tick = cycle;
      auto valid = function<bool (*)(void*, void*, const char*)>(host, "ValidatePtr2");
      if ((host.current_project && host.current_project() != project) || (valid && !valid(project, track, "MediaTrack*"))) {
        error = REAWEB_STREAM_CLOSED; return;
      }
      if (aggregate && std::chrono::steady_clock::now() >= deadline) return;
      auto refresh = function<bool (*)(void*)>(host, "AudioAccessorValidateState"); if (accessor && refresh) refresh(accessor);
      auto at = function<double (*)()>(host, "GetPlayPosition"); auto state = function<int (*)()>(host, "GetPlayState");
      if (!state || !(state() & 1)) at = function<double (*)()>(host, "GetCursorPosition");
      if (!pending) position = at ? at() : 0;
      std::fill(samples.begin(), samples.end(), 0);
      auto read = function<int (*)(void*, int, int, double, int, double*)>(host, "GetAudioAccessorSamples");
      const auto result = aggregate ? aggregate->read(rate, 2, position, frames(), samples.data(), deadline) :
        read(accessor, rate, 2, position, frames(), samples.data());
      pending = aggregate && result == 2;
      if (result < 0) error = REAWEB_STREAM_CLOSED;
      else if (!pending) {
        for (int i = 0; i < frames() * 2; ++i) floats[i] = static_cast<float>(samples[i]);
        ++sequence; timestamp = position;
        next_sample = std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<int64_t>(1000000 / update_rate));
      }
    }
  };
  struct Capture {
    StreamBuffer ring{REAWEB_AUDIO, audio_desc()};
    std::array<float, 8192 * 2> samples{};
    std::atomic<unsigned> users{0}, rate{0};
    std::atomic<uint64_t> callbacks{0}, unavailable{0};
    std::atomic<int> callback_frames{0}, callback_channels{0};
    std::atomic<bool> has_buffer{false};
    uint64_t sequence = 0, frames = 0;
  };
  struct Entry {
    uint64_t handle = 0;
    int window = 0, kind = 0, source = 0, device = -1;
    unsigned rate = 48000, fft_size = 2048;
    double update_rate = 30;
    std::string name, identity;
    void* track = nullptr;
    std::shared_ptr<TrackSource> track_source;
    std::unique_ptr<StreamBuffer> input;
    std::unique_ptr<AudioAnalysis> analysis;
    std::atomic<bool> stopping{false};
    std::atomic<int> error{0};
    bool attached = false;
    uint64_t sequence = 0, source_sequence = 0, capture_sequence = 0;
    std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now(), next_output{};
  };
  const Host& host;
  StreamHub& streams;
  Capture capture[2];
  std::mutex mutex;
  std::vector<std::shared_ptr<Entry>> entries;
  std::atomic<bool> stopping{false}, worker_failed{false};
  std::thread worker;
  uint64_t counter = 0, tick_sequence = 0;
  int last_midi = 0;
  bool midi_initialized = false;
  explicit Impl(const Host& h, StreamHub& s) : host(h), streams(s), worker([this] { run(); }) {}
  ~Impl() {
    stopping = true; worker.join();
    for (auto& entry : entries) { entry->track_source.reset(); streams.close(entry->handle); }
  }
  void close(const std::shared_ptr<Entry>& entry) {
    entry->stopping = true;
    entry->track_source.reset();
    if (entry->kind != REAWEB_MIDI && entry->source < 2) --capture[entry->source].users;
    streams.close(entry->handle, entry->error ? entry->error.load() : REAWEB_STREAM_CLOSED);
  }
  void reap() {
    std::vector<std::shared_ptr<Entry>> retired;
    const auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(mutex);
      for (auto it = entries.begin(); it != entries.end();) {
        auto& entry = **it; const auto users = streams.consumers(entry.handle);
        entry.attached = entry.attached || users > 0;
        if (entry.error || (!users && (entry.attached || now - entry.created > std::chrono::seconds(10)))) {
          entry.stopping = true; retired.push_back(*it); it = entries.erase(it);
        } else ++it;
      }
    }
    for (const auto& entry : retired) close(entry);
  }
  void run() noexcept {
    try {
      while (!stopping) {
        std::vector<std::shared_ptr<Entry>> active;
        { std::lock_guard<std::mutex> lock(mutex); active = entries; }
        for (int source = 0; source < 2; ++source) {
          StreamBuffer::Packet packet;
          for (int i = 0; i < 16 && capture[source].ring.consume(packet); ++i)
            for (const auto& entry : active) if (!entry->stopping && entry->source == source && entry->kind != REAWEB_MIDI)
              analyze(*entry, packet);
        }
        for (const auto& entry : active) if (!entry->stopping && entry->input) {
          StreamBuffer::Packet packet; for (int i = 0; i < 16 && entry->input->consume(packet); ++i) analyze(*entry, packet);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex); worker_failed = true;
      for (const auto& entry : entries) if (entry->kind != REAWEB_MIDI) entry->error = REAWEB_SERVICE_ERROR;
    }
  }
  void analyze(Entry& entry, const StreamBuffer::Packet& packet) noexcept {
    if (entry.error) return;
    try { analyze_packet(entry, packet); }
    catch (...) { entry.error = REAWEB_SERVICE_ERROR; }
  }
  void analyze_packet(Entry& entry, const StreamBuffer::Packet& packet) {
    if (entry.source < 2 && capture[entry.source].rate.load() != entry.rate) { entry.error = REAWEB_UNSUPPORTED_FORMAT; return; }
    if (entry.kind == REAWEB_AUDIO) {
      streams.publish(REAWEB_AUDIO, entry.handle, packet.data.data(), static_cast<uint32_t>(packet.data.size()), packet.sequence, packet.timestamp); return;
    }
    if (entry.capture_sequence && packet.sequence != entry.capture_sequence + 1) entry.analysis.reset();
    entry.capture_sequence = packet.sequence;
    if (!entry.analysis) entry.analysis = std::make_unique<AudioAnalysis>(entry.rate, 2, entry.fft_size);
    entry.analysis->process(reinterpret_cast<const float*>(packet.data.data()), packet.data.size() / 8);
    const auto now = std::chrono::steady_clock::now(); if (now < entry.next_output) return;
    entry.next_output = now + std::chrono::microseconds(static_cast<int64_t>(1000000 / entry.update_rate));
    auto result = entry.kind == REAWEB_SPECTRUM ? entry.analysis->spectrum() :
                  entry.kind == REAWEB_METER ? entry.analysis->meter() : entry.analysis->waveform(256);
    streams.publish(entry.kind, entry.handle, result.data(), static_cast<uint32_t>(result.size() * 4), ++entry.sequence, packet.timestamp);
  }
};
NativeProducers::NativeProducers(const Host& host, StreamHub& streams) : impl_(std::make_unique<Impl>(host, streams)) {}
NativeProducers::~NativeProducers() = default;
std::string NativeProducers::audio(const std::string& kind, const Json& options, int window) {
  auto& p = *impl_; auto entry = std::make_shared<Impl::Entry>();
  if (p.worker_failed) throw Error("NATIVE_ERROR", "Audio analysis worker unavailable");
  p.reap();
  { std::lock_guard<std::mutex> lock(p.mutex); if (p.entries.size() >= 8) throw Error("QUEUE_LIMIT", "At most eight built-in producers may be active"); }
  if (!options.is_object()) throw Error("INVALID_ARGUMENT", "Expected audio stream options");
  for (const auto& value : options.items()) if (value.key() != "source" && value.key() != "fftSize" && value.key() != "updateRate" && value.key() != "aggregate")
    throw Error("INVALID_ARGUMENT", "Unknown audio stream option: " + value.key());
  if (!options.value("source", Json("master")).is_string() || !options.value("fftSize", Json(2048)).is_number_integer() ||
      !options.value("updateRate", Json(30)).is_number() || !options.value("aggregate", Json(false)).is_boolean())
    throw Error("INVALID_ARGUMENT", "Invalid audio stream options");
  const auto source = options.value("source", std::string("master"));
  const bool aggregate = options.value("aggregate", false);
  if (aggregate && source != "selected-track" && source.rfind("track:", 0) != 0)
    throw Error("INVALID_ARGUMENT", "aggregate requires selected-track or track:<GUID>");
  const auto fft = options.value("fftSize", 2048);
  const auto update = options.value("updateRate", 30.0);
  if (fft < 32 || fft > 32768 || (fft & (fft - 1)) || !std::isfinite(update) || update < 1 || update > 120)
    throw Error("INVALID_ARGUMENT", "FFT size must be a power of two from 32 to 32768; updateRate must be 1..120");
  entry->kind = kind == "audio" ? REAWEB_AUDIO : kind == "spectrum" ? REAWEB_SPECTRUM : kind == "meter" ? REAWEB_METER : kind == "waveform" ? REAWEB_WAVEFORM : 0;
  if (!entry->kind) throw Error("UNSUPPORTED_FORMAT", "Expected audio, spectrum, meter or waveform");
  entry->source = source == "master" ? 1 : source == "input" ? 0 : 2; entry->window = window; entry->fft_size = fft; entry->update_rate = update;
  const auto device = function<bool (*)(const char*, char*, int)>(p.host, "GetAudioDeviceInfo");
  char rate[64]{};
  if (device && device("SRATE", rate, sizeof(rate))) {
    const double value = std::strtod(rate, nullptr); if (value >= 8000 && value <= 768000) entry->rate = static_cast<unsigned>(value);
  } else if (entry->source < 2) throw Error("AUDIO_UNAVAILABLE", "No active hardware audio device");
  entry->identity = source == "master" ? "hardware-output:0,1" : source == "input" ? "hardware-input:0,1" : source;
  if (entry->source == 2) {
    if (source == "selected-track") {
      auto selected = function<void* (*)(void*, int)>(p.host, "GetSelectedTrack"); if (selected) entry->track = selected(nullptr, 0);
    } else if (source.rfind("track:", 0) == 0 && p.host.track_count && p.host.track_identity) {
      auto get = function<void* (*)(void*, int)>(p.host, "GetTrack");
      for (int i = 0; get && i < p.host.track_count(); ++i) if (p.host.track_identity(i) == source.substr(6)) { entry->track = get(nullptr, i); break; }
    } else throw Error("INVALID_ARGUMENT", "Source must be master, input, selected-track or track:<GUID>");
    if (!entry->track) throw Error("INVALID_HANDLE", "Audio source track not available");
    if (source == "selected-track" && p.host.track_identity) {
      auto get = function<void* (*)(void*, int)>(p.host, "GetTrack");
      for (int i = 0; get && p.host.track_count && i < p.host.track_count(); ++i)
        if (get(nullptr, i) == entry->track) { entry->identity = "track:" + p.host.track_identity(i); break; }
    }
    auto create = function<void* (*)(void*)>(p.host, "CreateTrackAudioAccessor");
    if (!create || !function<void (*)(void*)>(p.host, "DestroyAudioAccessor") ||
        !function<int (*)(void*, int, int, double, int, double*)>(p.host, "GetAudioAccessorSamples"))
      throw Error("API_UNAVAILABLE", "Track audio accessors unavailable");
    entry->identity += ":pre-fx";
    if (aggregate) entry->identity += ":aggregate-source";
    const auto project = p.host.current_project ? p.host.current_project() : nullptr;
    for (const auto& other : p.entries) {
      const auto& shared = other->track_source;
      if (shared && !shared->error && shared->track == entry->track && shared->project == project &&
          shared->identity == entry->identity && shared->rate == entry->rate && shared->update_rate == update) {
        entry->track_source = shared; break;
      }
    }
    if (!entry->track_source) {
      entry->track_source = std::make_shared<Impl::TrackSource>(p.host, entry->track, entry->identity, entry->rate, update);
      if (aggregate) entry->track_source->aggregate = std::make_unique<AggregateSource>(p.host, entry->track);
      else {
        entry->track_source->accessor = create(entry->track);
        if (!entry->track_source->accessor) throw Error("AUDIO_UNAVAILABLE", "Cannot create track audio accessor");
      }
    }
    entry->input = std::make_unique<StreamBuffer>(REAWEB_AUDIO, audio_desc(entry->rate));
  }
  auto desc = audio_desc(entry->rate); desc.fft_size = fft; desc.update_rate = update; desc.source = entry->identity.c_str();
  if (entry->kind == REAWEB_SPECTRUM) desc.max_bytes = (fft / 2 + 1) * 2 * 4;
  if (entry->kind == REAWEB_METER) desc.max_bytes = 8 * 4;
  if (entry->kind == REAWEB_WAVEFORM) desc.max_bytes = std::min(256, fft) * 2 * 2 * 4;
  if (entry->kind != REAWEB_AUDIO) desc.capacity = 3;
  entry->name = "runtime.audio." + std::to_string(++p.counter);
  int status;
  { std::lock_guard<std::mutex> lock(p.mutex); status = p.entries.size() < 8 ? p.streams.create(entry->kind, entry->name.c_str(), &desc, &entry->handle) : REAWEB_QUEUE_LIMIT;
    if (!status) { if (entry->source < 2) ++p.capture[entry->source].users;
      if (p.worker_failed) entry->error = REAWEB_SERVICE_ERROR;
      p.entries.push_back(entry); } }
  if (status) throw Error(StreamHub::code(status), "Cannot create audio analysis stream");
  return entry->name;
}
std::string NativeProducers::midi(int device, int window) {
  auto& p = *impl_;
  if (device < -1 || device > 65535) throw Error("INVALID_ARGUMENT", "Expected -1 for all MIDI inputs or a device index");
  if (!function<int (*)(int, char*, int*, int*, int*, double*, int*)>(p.host, "MIDI_GetRecentInputEvent")) throw Error("API_UNAVAILABLE", "MIDI input history unavailable");
  auto entry = std::make_shared<Impl::Entry>(); entry->kind = REAWEB_MIDI; entry->window = window; entry->device = device;
  entry->name = "runtime.midi." + std::to_string(++p.counter); entry->identity = "midi-input:" + std::to_string(device);
  ReaWeb_StreamDesc desc{}; desc.size = sizeof(desc); desc.abi_version = 1; desc.format = REAWEB_BYTES; desc.max_bytes = 4096; desc.capacity = 64; desc.source = entry->identity.c_str();
  std::lock_guard<std::mutex> lock(p.mutex);
  if (p.entries.size() >= 8) throw Error("QUEUE_LIMIT", "At most eight built-in producers may be active");
  const auto status = p.streams.create(REAWEB_MIDI, entry->name.c_str(), &desc, &entry->handle);
  if (status) throw Error(StreamHub::code(status), "Cannot create MIDI stream");
  p.entries.push_back(entry); return entry->name;
}
void NativeProducers::capture(bool output, int frames, double rate, int channels, double* (*get)(bool, int)) noexcept {
  auto& capture = impl_->capture[output ? 1 : 0];
  ++capture.callbacks;
  capture.callback_frames = frames; capture.callback_channels = channels; capture.has_buffer = get != nullptr;
  if (!capture.users.load(std::memory_order_relaxed) || !get || frames < 1 || frames > 8192 || rate < 8000 || rate > 768000) return;
  capture.rate.store(static_cast<unsigned>(rate), std::memory_order_relaxed);
  auto* left = get(output, 0); if (!left) { ++capture.unavailable; return; }
  auto* right = channels == 1 ? left : get(output, 1); if (!right) right = left;
  for (int i = 0; i < frames; ++i) { capture.samples[i * 2] = static_cast<float>(left[i]); capture.samples[i * 2 + 1] = static_cast<float>(right[i]); }
  capture.ring.publish(capture.samples.data(), static_cast<uint32_t>(frames * 8), ++capture.sequence, capture.frames / rate);
  capture.frames += frames;
}
void NativeProducers::attached(const std::string& name) {
  auto& p = *impl_; std::lock_guard<std::mutex> lock(p.mutex);
  for (const auto& entry : p.entries) if (entry->name == name) entry->attached = true;
}
void NativeProducers::tick() {
  auto& p = *impl_; p.reap();
  std::vector<std::shared_ptr<Impl::Entry>> active;
  { std::lock_guard<std::mutex> lock(p.mutex); active = p.entries; }
  const auto now = std::chrono::steady_clock::now();
  const auto deadline = now + std::chrono::milliseconds(2);
  ++p.tick_sequence;
  for (size_t i = 0; i < active.size(); ++i) {
    auto& entry = *active[(i + p.tick_sequence - 1) % active.size()];
    if (!entry.attached || !entry.track_source) continue;
    auto& source = *entry.track_source;
    source.sample(p.tick_sequence, now, deadline);
    if (source.error) entry.error = source.error;
    else if (entry.source_sequence != source.sequence) {
      entry.source_sequence = source.sequence;
      entry.input->publish(source.floats.data(), source.frames() * 8, entry.source_sequence, source.timestamp);
    }
  }
  if (std::none_of(p.entries.begin(), p.entries.end(), [](const auto& e) { return e->kind == REAWEB_MIDI; })) { p.midi_initialized = false; return; }
  auto recent = function<int (*)(int, char*, int*, int*, int*, double*, int*)>(p.host, "MIDI_GetRecentInputEvent");
  struct MIDI { int sequence, timestamp, device; double position; std::vector<unsigned char> data; };
  std::vector<MIDI> messages;
  for (int index = 0; index < 256; ++index) {
    char buffer[1024]; int size = sizeof(buffer), stamp = 0, device = 0, loop = 0; double position = 0;
    const auto sequence = recent(index, buffer, &size, &stamp, &device, &position, &loop);
    if (!sequence || sequence == p.last_midi) break;
    if (!p.midi_initialized) { p.last_midi = sequence; p.midi_initialized = true; break; }
    if (size > 0 && size <= int(sizeof(buffer))) messages.push_back({sequence, stamp, device, position, {buffer, buffer + size}});
  }
  p.midi_initialized = true;
  if (messages.empty()) return;
  p.last_midi = messages.front().sequence;
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    std::vector<unsigned char> bytes(16 + it->data.size());
    const auto put = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(value >> (i * 8)); };
    put(0, it->device); put(4, it->timestamp); put(8, static_cast<uint32_t>(it->data.size())); put(12, 0);
    std::copy(it->data.begin(), it->data.end(), bytes.begin() + 16);
    for (const auto& entry : p.entries) if (entry->kind == REAWEB_MIDI && (entry->device == -1 || entry->device == (it->device & 65535)))
      p.streams.publish(REAWEB_MIDI, entry->handle, bytes.data(), static_cast<uint32_t>(bytes.size()), static_cast<uint32_t>(it->sequence), it->position);
  }
}
void NativeProducers::close_window(int window) {
  auto& p = *impl_; std::lock_guard<std::mutex> lock(p.mutex);
  for (auto it = p.entries.begin(); it != p.entries.end();) {
    if ((*it)->window != window) { ++it; continue; }
    if (p.streams.consumers((*it)->handle)) { (*it)->window = 0; ++it; }
    else { p.close(*it); it = p.entries.erase(it); }
  }
}
Json NativeProducers::devices() const {
  const auto& host = impl_->host; Json result{{"audio", Json::object()}, {"midiInputs", Json::array()}, {"midiOutputs", Json::array()}};
  auto audio = function<bool (*)(const char*, char*, int)>(host, "GetAudioDeviceInfo");
  for (const auto* key : {"MODE", "IDENT_IN", "IDENT_OUT", "BSIZE", "SRATE", "BPS"}) {
    char value[1024]{}; result["audio"][key] = audio && audio(key, value, sizeof(value)) ? Json(value) : Json();
  }
  for (const bool input : {true, false}) {
    auto channel_count = function<int (*)()>(host, input ? "GetNumAudioInputs" : "GetNumAudioOutputs");
    auto channel_name = function<const char* (*)(int)>(host, input ? "GetInputChannelName" : "GetOutputChannelName");
    auto& channels = result["audio"][input ? "inputs" : "outputs"]; channels = Json::array();
    for (int i = 0, total = channel_count ? std::min(256, channel_count()) : 0; i < total; ++i) {
      const auto* value = channel_name ? channel_name(i) : nullptr;
      channels.push_back({{"id", i}, {"name", value ? value : ""}});
    }
    auto count = function<int (*)()>(host, input ? "GetNumMIDIInputs" : "GetNumMIDIOutputs");
    auto name = function<bool (*)(int, char*, int)>(host, input ? "GetMIDIInputName" : "GetMIDIOutputName");
    if (!count || !name) continue;
    for (int i = 0, total = std::min(256, count()); i < total; ++i) { char text[1024]{}; const bool present = name(i, text, sizeof(text));
      if (!present && !*text) continue;
      result[input ? "midiInputs" : "midiOutputs"].push_back({{"id", i}, {"name", text}, {"present", present}}); }
  }
  return result;
}
Json NativeProducers::diagnostics() const {
  Json result = Json::array();
  for (const auto& source : impl_->capture) result.push_back({{"callbacks", source.callbacks.load()}, {"users", source.users.load()},
    {"sampleRate", source.rate.load()}, {"published", source.ring.published.load()}, {"dropped", source.ring.dropped.load()}, {"unavailable", source.unavailable.load()},
    {"blockFrames", source.callback_frames.load()}, {"channels", source.callback_channels.load()}, {"bufferAvailable", source.has_buffer.load()}});
  return result;
}
}
