#include "runtime/track_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <eel2/ns-eel.h>

namespace reaweb {
namespace {
template<class F> F require(const Host& host, const char* name) {
  const auto result = host.native_function ? host.native_function(name) : nullptr;
  if (!result) throw Error("API_UNAVAILABLE", std::string("Track capture requires ") + name);
  return reinterpret_cast<F>(result);
}
constexpr const char* effect = "ReaWebAPI/track_audio_v1.jsfx";
constexpr const char* memory = "ReaWebAPI.TrackAudio.v1";
constexpr unsigned packet_size = 16392, capacity = 16, slot_size = 8 + packet_size * capacity;
struct Atomics {
  void (*enter)();
  void (*leave)();
  eel_function_table functions{};
  static double NSEEL_CGEN_CALL set(void* context, double* target, double* value) {
    auto& a = *static_cast<Atomics*>(context);
    a.enter(); const double result = *target = *value; a.leave(); return result;
  }
  static double NSEEL_CGEN_CALL compare(void* context, double* target, double* expected, double* value) {
    auto& a = *static_cast<Atomics*>(context);
    a.enter(); const double result = *target; if (result == *expected) *target = *value; a.leave(); return result;
  }
  explicit Atomics(const Host& host) : enter(require<void (*)()>(host, "NSEEL_HOSTSTUB_EnterMutex")),
    leave(require<void (*)()>(host, "NSEEL_HOSTSTUB_LeaveMutex")) {
    const auto add = require<decltype(&NSEEL_addfunc_ret_type)>(host, "NSEEL_addfunc_ret_type");
    const auto scope = require<NSEEL_PPPROC>(host, "NSEEL_PProc_THIS");
    add("atomic_set", 2, 1, scope, reinterpret_cast<void*>(set), &functions);
    add("atomic_setifequal", 3, 1, scope, reinterpret_cast<void*>(compare), &functions);
  }
};
constexpr const char* script = R"JS(desc:ReaWebAPI Track Audio (temporary)
options:gmem=ReaWebAPI.TrackAudio.v1
options:no_meter
slider1:0<0,2147483647,1>-Capture token
slider2:0<0,7,1>-Capture slot
in_pin:left input
in_pin:right input
out_pin:left output
out_pin:right output

@init
ext_noinit = 1;
ext_nodenorm = 1;
write_index = 0;
sequence = 0;
writing = 0;

@block
base = floor(slider2) * 262280;
track_index = get_host_placement(chain_position, placement_flags);
enabled = slider1 > 0 && atomic_get(gmem[base]) == slider1
  && track_index == atomic_get(gmem[base+1])
  && chain_position == atomic_get(gmem[base+2]) && placement_flags == 0;
sequence += 1;
writing = 0;
enabled && samplesblock > 0 && samplesblock <= 8192 ? (
  packet = base + 8 + write_index * 16392;
  atomic_setifequal(gmem[packet], 0, 1) == 0 ? (
    gmem[packet+1] = samplesblock;
    gmem[packet+2] = srate;
    gmem[packet+3] = sequence;
    gmem[packet+4] = play_position;
    sample_index = 0;
    writing = 1;
  );
);

@sample
writing ? (
  gmem[packet+8+sample_index*2] = spl0;
  gmem[packet+9+sample_index*2] = spl1;
  sample_index += 1;
  sample_index == samplesblock ? (
    atomic_set(gmem[packet], 2);
    write_index = (write_index + 1) % 16;
    writing = 0;
  );
);
)JS";
}

struct TrackAudio::Impl {
  const Host& host;
  void* track;
  void* project;
  uint64_t generation;
  Guid guid{}, fx_guid{};
  bool inserted = false;
  void* vm = nullptr;
  void* reader = nullptr;
  void* control = nullptr;
  double *base = nullptr, *token = nullptr, *track_index = nullptr, *fx_index = nullptr;
  void (*free_vm)(void*);
  void (*free_code)(void*);
  void (*execute)(void*);
  double* (*ram)(void*, unsigned, int*);
  double (*track_value)(void*, const char*);
  int (*fx_count)(void*);
  void* (*fx_id)(void*, int);
  bool (*delete_fx)(void*, int);
  bool (*fx_enabled)(void*, int);
  bool (*fx_offline)(void*, int);
  void (*move_fx)(void*, int, void*, int, bool);
  bool (*validate)(void*, void*, const char*);

  Impl(const Host& h, void* t) : host(h), track(t), project(h.current_project ? h.current_project() : nullptr),
    generation(h.project_generation ? h.project_generation() : 0),
    free_vm(require<void (*)(void*)>(h, "NSEEL_VM_free")),
    free_code(require<void (*)(void*)>(h, "NSEEL_code_free")),
    execute(require<void (*)(void*)>(h, "NSEEL_code_execute")),
    ram(require<double* (*)(void*, unsigned, int*)>(h, "NSEEL_VM_getramptr")),
    track_value(require<double (*)(void*, const char*)>(h, "GetMediaTrackInfo_Value")),
    fx_count(require<int (*)(void*)>(h, "TrackFX_GetCount")),
    fx_id(require<void* (*)(void*, int)>(h, "TrackFX_GetFXGUID")),
    delete_fx(require<bool (*)(void*, int)>(h, "TrackFX_Delete")),
    fx_enabled(require<bool (*)(void*, int)>(h, "TrackFX_GetEnabled")),
    fx_offline(require<bool (*)(void*, int)>(h, "TrackFX_GetOffline")),
    move_fx(require<void (*)(void*, int, void*, int, bool)>(h, "TrackFX_CopyToTrack")),
    validate(require<bool (*)(void*, void*, const char*)>(h, "ValidatePtr2")) {
    if (host.track_guid) guid = host.track_guid(track);
  }
  bool track_valid() const {
    return (!project || validate(nullptr, project, "ReaProject*")) && validate(project, track, "MediaTrack*") &&
      (!host.track_guid || host.track_guid(track) == guid);
  }
  int index() const {
    if (!inserted || !track_valid()) return -1;
    for (int i = 0; i < fx_count(track); ++i) {
      const auto id = fx_id(track, i);
      if (id && !std::memcmp(id, fx_guid.data(), fx_guid.size())) return i;
    }
    return -1;
  }
  ~Impl() {
    if (control) { *token = 0; execute(control); }
    const int fx = index();
    if (fx >= 0) delete_fx(track, fx);
    if (reader) free_code(reader);
    if (control) free_code(control);
    if (vm) free_vm(vm);
  }
};

TrackAudio::TrackAudio(const Host& host, void* track, unsigned slot, uint64_t token) : impl_(std::make_unique<Impl>(host, track)) {
  auto& p = *impl_;
  if (!p.track_value(track, "I_FXEN")) throw Error("AUDIO_UNAVAILABLE", "Track FX chain is bypassed");
  const auto allocate = require<void* (*)()>(host, "NSEEL_VM_alloc");
  const auto attach = require<void** (*)(const char*, bool)>(host, "eel_gmem_attach");
  const auto gram = require<void (*)(void*, void**)>(host, "NSEEL_VM_SetGRAM");
  const auto variable = require<double* (*)(void*, const char*)>(host, "NSEEL_VM_regvar");
  const auto compile = require<void* (*)(void*, const char*, int, int)>(host, "NSEEL_code_compile_ex");
  const auto compile_error = require<const char* (*)(void*)>(host, "NSEEL_code_getcodeerror");
  const auto resource = require<const char* (*)()>(host, "GetResourcePath");
  const auto add = require<int (*)(void*, const char*, bool, int)>(host, "TrackFX_AddByName");
  const auto parameter = require<bool (*)(void*, int, int, double)>(host, "TrackFX_SetParam");
  if (slot >= 8 || !token || token > 2147483647) throw Error("QUEUE_LIMIT", "Track capture slot limit exceeded");
  p.vm = allocate();
  auto space = attach(memory, true);
  if (!p.vm || !space) throw Error("AUDIO_UNAVAILABLE", "Cannot allocate track capture memory");
  gram(p.vm, space);
  // Use the host EEL engine and allocator, never duplicate its shared-memory layout.
  static Atomics atomics(host);
  require<decltype(&NSEEL_VM_SetFunctionTable)>(host, "NSEEL_VM_SetFunctionTable")(p.vm, &atomics.functions);
  require<decltype(&NSEEL_VM_SetCustomFuncThis)>(host, "NSEEL_VM_SetCustomFuncThis")(p.vm, &atomics);
  p.base = variable(p.vm, "base"); p.token = variable(p.vm, "token");
  p.track_index = variable(p.vm, "track_index"); p.fx_index = variable(p.vm, "fx_index");
  if (!p.base || !p.token || !p.track_index || !p.fx_index) throw Error("AUDIO_UNAVAILABLE", "Cannot initialize track capture");
  *p.base = slot * slot_size; *p.token = 0;
  p.control = compile(p.vm, "atomic_set(gmem[base],token); atomic_set(gmem[base+1],track_index); atomic_set(gmem[base+2],fx_index);", 0, 0);
  if (!p.control) throw Error("API_UNAVAILABLE", std::string("Cannot compile track capture control: ") + compile_error(p.vm));
  p.reader = compile(p.vm, R"EEL(
    0[0]=0;
    packet=base+8+read_index*16392;
    atomic_setifequal(gmem[packet],2,3)==2 ? (
      frames=gmem[packet+1];
      frames>0 && frames<=8192 && frames==floor(frames) ? (
        0[0]=frames; 1[0]=gmem[packet+2]; 2[0]=gmem[packet+3]; 3[0]=gmem[packet+4];
        i=0; loop(frames*2, (i+4)[0]=gmem[packet+8+i]; i+=1;);
      );
      atomic_set(gmem[packet],0);
      read_index=(read_index+1)%16;
    );
  )EEL", 0, 0);
  if (!p.reader) throw Error("API_UNAVAILABLE", std::string("Cannot compile track capture reader: ") + compile_error(p.vm));
  const auto init = compile(p.vm, "atomic_set(gmem[base],0); i=0; loop(262280,gmem[base+i]=0;i+=1;); read_index=0;", 0, 0);
  if (!p.control || !p.reader || !init) { if (init) p.free_code(init); throw Error("API_UNAVAILABLE", "Cannot compile track capture reader"); }
  p.execute(init); p.free_code(init);
  const auto path = fs::u8path(resource()) / "Effects" / effect;
  fs::create_directories(path.parent_path());
  std::ifstream existing(path, std::ios::binary);
  const std::string contents{std::istreambuf_iterator<char>(existing), {}};
  existing.close();
  if (contents != script) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc); output << script;
    if (!output) throw Error("AUDIO_UNAVAILABLE", "Cannot install track capture JSFX");
  }
  const auto named = require<bool (*)(void*, int, const char*, char*, int)>(host, "TrackFX_GetNamedConfigParm");
  // Saved/undo-restored captures have no live owner. Replace only our exact JSFX identifier.
  for (int i = p.fx_count(track) - 1; i >= 0; --i) {
    char ident[1024]{};
    if (named(track, i, "fx_ident", ident, sizeof(ident))) {
      std::replace(std::begin(ident), std::end(ident), '\\', '/');
      if (std::string(ident) == effect) p.delete_fx(track, i);
    }
  }
  const auto fx = add(track, ("JS: " + std::string(effect)).c_str(), false, -1);
  if (fx < 0) throw Error("AUDIO_UNAVAILABLE", "Cannot insert track capture JSFX");
  const auto id = p.fx_id(track, fx);
  if (!id) { p.delete_fx(track, fx); throw Error("AUDIO_UNAVAILABLE", "Cannot identify track capture JSFX"); }
  std::memcpy(p.fx_guid.data(), id, p.fx_guid.size()); p.inserted = true;
  if (!parameter(track, fx, 0, double(token)) || !parameter(track, fx, 1, slot))
    throw Error("AUDIO_UNAVAILABLE", "Cannot configure track capture JSFX");
  *p.token = double(token); *p.track_index = p.track_value(track, "IP_TRACKNUMBER") - 1; *p.fx_index = fx;
  p.execute(p.control);
}
TrackAudio::~TrackAudio() = default;
bool TrackAudio::valid() {
  auto& p = *impl_;
  if ((p.host.current_project && p.host.current_project() != p.project) ||
      (p.host.project_generation && p.host.project_generation() != p.generation)) return false;
  const auto fx = p.index();
  if (fx < 0 || !p.track_value(p.track, "I_FXEN") || !p.fx_enabled(p.track, fx) || p.fx_offline(p.track, fx)) return false;
  if (fx != p.fx_count(p.track) - 1) p.move_fx(p.track, fx, p.track, p.fx_count(p.track), true);
  const auto last = p.index();
  if (last < 0 || last != p.fx_count(p.track) - 1) return false;
  *p.track_index = p.track_value(p.track, "IP_TRACKNUMBER") - 1; *p.fx_index = last;
  p.execute(p.control);
  return true;
}
bool TrackAudio::read(std::vector<float>& samples, unsigned& rate, uint64_t& sequence, double& position) {
  auto& p = *impl_; p.execute(p.reader);
  int count = 0; auto header = p.ram(p.vm, 0, &count);
  if (!header || count < 4 || header[0] == 0) return false;
  if (!std::isfinite(header[1]) || header[1] < 8000 || header[1] > 768000 ||
      !std::isfinite(header[2]) || header[2] < 1 || !std::isfinite(header[3]))
    throw Error("AUDIO_INVALID_DATA", "Invalid track capture packet");
  const auto size = static_cast<unsigned>(header[0]) * 2;
  rate = static_cast<unsigned>(header[1]); sequence = static_cast<uint64_t>(header[2]); position = header[3];
  samples.resize(size);
  for (unsigned at = 0; at < size;) {
    auto data = p.ram(p.vm, at + 4, &count);
    if (!data || count < 1) throw Error("AUDIO_INVALID_DATA", "Incomplete track capture packet");
    const auto length = (std::min)(size - at, static_cast<unsigned>(count));
    for (unsigned i = 0; i < length; ++i) samples[at + i] = static_cast<float>(data[i]);
    at += length;
  }
  return true;
}
}
