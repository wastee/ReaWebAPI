#include "runtime/aggregate_source.hpp"
#include <algorithm>
#include <vector>

namespace reaweb {
namespace {
template<class F> F require(const Host& host, const char* name) {
  auto fn = reinterpret_cast<F>(host.native_function ? host.native_function(name) : nullptr);
  if (!fn) throw Error("API_UNAVAILABLE", std::string("Aggregate source requires ") + name);
  return fn;
}
struct Node {
  void* track;
  Guid guid;
  int parent = -1, solo = 0;
  bool main = false, muted = false, defeat = false;
  std::vector<int> inputs, outputs;
};
void visit(const std::vector<Node>& nodes, std::vector<bool>& seen, std::vector<int> pending, bool upstream) {
  while (!pending.empty()) {
    const int index = pending.back(); pending.pop_back();
    if (seen[index] || nodes[index].muted) continue;
    seen[index] = true;
    const auto& next = upstream ? nodes[index].inputs : nodes[index].outputs;
    pending.insert(pending.end(), next.begin(), next.end());
  }
}
}
struct AggregateSource::Impl {
  const Host& host;
  void* root;
  void* project;
  Guid root_guid;
  void* (*get)(void*, int);
  void* (*parent)(void*);
  double (*value)(void*, const char*);
  int (*sends)(void*, int);
  void* (*send)(void*, int, int, const char*, void*);
  double (*send_value)(void*, int, int, const char*);
  void* (*create)(void*);
  void (*destroy)(void*);
  bool (*refresh)(void*);
  int (*samples)(void*, int, int, double, int, double*);
  struct Source { Guid guid; void* accessor; };
  std::unordered_map<void*, Source> sources;
  std::vector<double> scratch;

  Impl(const Host& h, void* r) : host(h), root(r) {
    if (!host.current_project || !host.track_count || !host.track_guid)
      throw Error("API_UNAVAILABLE", "Aggregate source requires project and track identity");
    project = host.current_project(); root_guid = host.track_guid(root);
    get = require<decltype(get)>(host, "GetTrack");
    parent = require<decltype(parent)>(host, "GetParentTrack");
    value = require<decltype(value)>(host, "GetMediaTrackInfo_Value");
    sends = require<decltype(sends)>(host, "GetTrackNumSends");
    send = require<decltype(send)>(host, "GetSetTrackSendInfo");
    send_value = require<decltype(send_value)>(host, "GetTrackSendInfo_Value");
    create = require<decltype(create)>(host, "CreateTrackAudioAccessor");
    destroy = require<decltype(destroy)>(host, "DestroyAudioAccessor");
    refresh = require<decltype(refresh)>(host, "AudioAccessorValidateState");
    samples = require<decltype(samples)>(host, "GetAudioAccessorSamples");
  }
  ~Impl() { for (const auto& source : sources) destroy(source.second.accessor); }

  bool collect(std::vector<Node>& nodes, std::vector<bool>& included) {
    if (host.current_project() != project) return false;
    std::unordered_map<void*, int> indices;
    int root_index = -1;
    const int count = host.track_count();
    nodes.reserve(count);
    bool solo = false;
    for (int i = 0; i < count; ++i) {
      auto* track = get(project, i);
      if (!track) continue;
      const int index = static_cast<int>(nodes.size());
      indices.emplace(track, index);
      nodes.push_back({track, host.track_guid(track)});
      auto& node = nodes.back();
      node.main = value(track, "B_MAINSEND") != 0;
      node.muted = value(track, "B_MUTE") != 0;
      node.solo = static_cast<int>(value(track, "I_SOLO"));
      node.defeat = value(track, "B_SOLO_DEFEAT") != 0;
      solo = solo || node.solo != 0;
      if (track == root && node.guid == root_guid) root_index = index;
    }
    if (root_index < 0) return false;
    for (auto& node : nodes) {
      const auto found = indices.find(parent(node.track));
      if (found != indices.end()) node.parent = found->second;
    }
    // Only an enabled parent-send chain inherits folder mute.
    std::vector<bool> muted(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
      int at = static_cast<int>(i);
      for (size_t depth = 0; at >= 0 && depth < nodes.size(); ++depth) {
        if (nodes[at].muted) { muted[i] = true; break; }
        at = nodes[at].main ? nodes[at].parent : -1;
      }
    }
    for (size_t i = 0; i < nodes.size(); ++i) nodes[i].muted = muted[i];
    const auto connect = [&](int from, int to) {
      if (nodes[from].muted || nodes[to].muted) return;
      nodes[to].inputs.push_back(from); nodes[from].outputs.push_back(to);
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes[i];
      if (node.main && node.parent >= 0) connect(static_cast<int>(i), node.parent);
      for (int j = 0, total = sends(node.track, -1); j < total; ++j) {
        if (send_value(node.track, -1, j, "B_MUTE") != 0 || send_value(node.track, -1, j, "I_SRCCHAN") < 0) continue;
        const auto found = indices.find(send(node.track, -1, j, "P_SRCTRACK", nullptr));
        if (found != indices.end()) connect(found->second, static_cast<int>(i));
      }
    }
    included.assign(nodes.size(), false);
    if (!solo) visit(nodes, included, {root_index}, true);
    else {
      std::vector<int> seeds;
      for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].solo || nodes[i].defeat) seeds.push_back(static_cast<int>(i));
      std::vector<bool> content(nodes.size()), in_place(nodes.size()), parent_path(nodes.size());
      visit(nodes, content, seeds, true);
      // A soloed source keeps its parent path open. Solo-in-place also opens sends.
      for (const int seed : seeds) {
        if ((nodes[seed].solo & 3) == 2 || nodes[seed].defeat) visit(nodes, in_place, {seed}, false);
        else {
          int at = seed;
          for (size_t depth = 0; at >= 0 && depth < nodes.size() && !nodes[at].muted; ++depth) {
            parent_path[at] = true; at = nodes[at].main ? nodes[at].parent : -1;
          }
        }
      }
      std::vector<bool> seen(nodes.size());
      std::vector<int> pending{root_index};
      while (!pending.empty()) {
        const int at = pending.back(); pending.pop_back();
        if (seen[at] || nodes[at].muted) continue;
        seen[at] = true; included[at] = content[at];
        for (const int from : nodes[at].inputs)
          if (content[at] || in_place[from] || (parent_path[from] && nodes[from].main && nodes[from].parent == at))
            pending.push_back(from);
      }
    }
    return true;
  }
};
AggregateSource::AggregateSource(const Host& host, void* root) : impl_(std::make_unique<Impl>(host, root)) {}
AggregateSource::~AggregateSource() = default;
int AggregateSource::read(int rate, int channels, double position, int frames, double* output) {
  auto& p = *impl_;
  std::vector<Node> nodes; std::vector<bool> included;
  if (!p.collect(nodes, included)) return -1;
  std::unordered_map<void*, Guid> wanted;
  for (size_t i = 0; i < nodes.size(); ++i) if (included[i]) wanted.emplace(nodes[i].track, nodes[i].guid);
  for (auto it = p.sources.begin(); it != p.sources.end();) {
    const auto found = wanted.find(it->first);
    if (found == wanted.end() || found->second != it->second.guid) { p.destroy(it->second.accessor); it = p.sources.erase(it); }
    else ++it;
  }
  const size_t size = static_cast<size_t>(frames) * channels;
  std::fill(output, output + size, 0); p.scratch.resize(size);
  for (size_t i = 0; i < nodes.size(); ++i) if (included[i]) {
    const auto& node = nodes[i];
    auto found = p.sources.find(node.track);
    if (found == p.sources.end()) {
      std::unique_ptr<void, void (*)(void*)> accessor(p.create(node.track), p.destroy);
      if (!accessor) return -1;
      found = p.sources.emplace(node.track, Impl::Source{node.guid, accessor.get()}).first;
      accessor.release();
    }
    p.refresh(found->second.accessor);
    std::fill(p.scratch.begin(), p.scratch.end(), 0);
    const int result = p.samples(found->second.accessor, rate, channels, position, frames, p.scratch.data());
    if (result < 0) return -1;
    if (result > 0) for (size_t sample = 0; sample < size; ++sample) output[sample] += p.scratch[sample];
  }
  return 1;
}
}
