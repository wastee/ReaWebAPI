#include "runtime/jsfx_native.hpp"
#include "runtime/audio_analysis.hpp"
#include "runtime/external_settings.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>

namespace reaweb {
namespace {
using Time = std::chrono::steady_clock;
using Engine = JsfxChannelEngine;
struct Retired {
  std::mutex mutex;
  std::vector<std::pair<std::shared_ptr<Engine>,Engine::Lease>> leases;
  void add(const std::shared_ptr<Engine>& engine,const Engine::Lease& lease) {
    if(lease.slot<0)return;
    std::lock_guard<std::mutex> lock(mutex);leases.emplace_back(engine,lease);
  }
  void collect() {
    std::lock_guard<std::mutex> lock(mutex);
    for(auto it=leases.begin();it!=leases.end();) {
      if(it->first->release(it->second))it=leases.erase(it);else ++it;
    }
  }
};
int kind_of(const std::string& name) {
  if(name=="audio")return REAWEB_AUDIO;
  if(name=="spectrum")return REAWEB_SPECTRUM;
  if(name=="waveform")return REAWEB_WAVEFORM;
  if(name=="meter")return REAWEB_METER;
  return name=="midi"?REAWEB_MIDI:REAWEB_BINARY;
}
}
struct JsfxNativeHub::Node::Impl {
  struct Action { uint64_t request;unsigned kind,id;std::vector<double> values;uint64_t owner=0;bool sent=false; };
  struct Stream {
    unsigned index=0,rate=0,channels=0,fft=2048,points=512;
    int kind=0,window=0;uint64_t handle=0,document=0,epoch=0,next_sample=0,sequence=0,history_frames=0;
    std::string name;
    Json descriptor;
    double update_rate=30;
    std::atomic<bool> enabled{true};
    bool attached=false,have_packet=false,pending_gap=false,output_gap=false;
    Time::time_point created=Time::now(),published{};
    std::unique_ptr<AudioAnalysis> analysis;
  };
  std::shared_ptr<Engine> engine;
  std::shared_ptr<Retired> retired;
  StreamHub& streams;
  std::shared_ptr<JsfxTransfer::Budget> budget;
  mutable std::mutex mutex;
  Engine::Lease lease;
  std::deque<Action> actions;
  std::vector<Result> completed;
  std::vector<std::shared_ptr<Stream>> outputs;
  std::vector<double> status;
  Time::time_point heartbeat{},next_status{};
  uint64_t received=0,invalid=0;
  unsigned subscriptions=256;
  std::string error;
  explicit Impl(std::shared_ptr<Engine> e,StreamHub& s):engine(std::move(e)),streams(s){}
  void retire() {if(retired)retired->add(engine,lease);lease={};}
  void close(int window=0) {
    for(auto it=outputs.begin();it!=outputs.end();) {
      if(!window || (*it)->window==window) {
        (*it)->enabled=false;streams.close((*it)->handle);it=outputs.erase(it);
      } else ++it;
    }
  }
  void publish(Stream& out,const Engine::Packet& packet) {
    if(!out.enabled || packet.id!=out.index)return;
    if(packet.channels!=out.channels || packet.sample_rate!=out.rate || packet.values.size()%out.channels) {
      out.pending_gap=true;return;
    }
    const auto frames=packet.values.size()/packet.channels;
    const bool discontinuity=out.pending_gap || !out.have_packet || out.epoch!=packet.clock_epoch || out.next_sample!=packet.first_sample;
    out.pending_gap=false;out.have_packet=true;out.epoch=packet.clock_epoch;out.next_sample=packet.first_sample+frames;
    out.output_gap=out.output_gap || discontinuity;
    StreamBuffer::Timing timing{packet.clock_epoch,packet.first_sample,packet.dropped,packet.position,unsigned(packet.play_state),out.output_gap};
    if(out.kind==REAWEB_BINARY || out.kind==REAWEB_MIDI) {
      std::vector<unsigned char> bytes;bytes.reserve(packet.values.size());
      for(double v:packet.values) {
        if(v<0 || v>255 || v!=std::floor(v)){out.pending_gap=true;return;}
        bytes.push_back(static_cast<unsigned char>(v));
      }
      out.output_gap=streams.publish(out.kind,out.handle,bytes.data(),uint32_t(bytes.size()),++out.sequence,
        double(timing.first_sample)/packet.sample_rate,&timing)!=REAWEB_OK;return;
    }
    std::vector<float> samples;samples.reserve(packet.values.size());
    for(double v:packet.values) {
      if(std::abs(v)>std::numeric_limits<float>::max()){out.pending_gap=true;return;}
      samples.push_back(static_cast<float>(v));
    }
    if(out.kind!=REAWEB_AUDIO) {
      if(!out.analysis || discontinuity) {
        out.analysis=std::make_unique<AudioAnalysis>(out.rate,out.channels,out.fft);out.history_frames=0;
      }
      out.analysis->process(samples.data(),frames);
      out.history_frames+=frames;
      const auto now=Time::now();
      if(out.history_frames<out.fft || now-out.published<std::chrono::duration<double>(1/out.update_rate))return;
      samples=out.kind==REAWEB_SPECTRUM?out.analysis->spectrum():
        out.kind==REAWEB_WAVEFORM?out.analysis->waveform(out.points):out.analysis->meter();
      out.published=now;
      timing.first_sample=out.next_sample-out.fft;
    }
    out.output_gap=streams.publish(out.kind,out.handle,samples.data(),uint32_t(samples.size()*sizeof(float)),++out.sequence,
      double(timing.first_sample)/packet.sample_rate,&timing)!=REAWEB_OK;
  }
};
JsfxNativeHub::Node::Node(JsfxNativeDeclaration value,std::shared_ptr<Engine> engine,StreamHub& streams)
  :declaration(std::move(value)),impl_(new Impl(std::move(engine),streams)){}
JsfxNativeHub::Node::~Node() {impl_->close();impl_->retire();}
bool JsfxNativeHub::Node::refresh(JsfxBinding& binding) {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  const auto now=Time::now();
  for(auto it=p.outputs.begin();it!=p.outputs.end();) {
    auto& out=**it;const auto count=p.streams.consumers(out.handle);
    out.attached=out.attached || count>0;
    if((out.attached && !count) || (!out.attached && now-out.created>std::chrono::seconds(10))) {
      out.enabled=false;p.streams.close(out.handle);it=p.outputs.erase(it);
    } else ++it;
  }
  if(p.lease.epoch && p.engine->valid(p.lease,binding,declaration.parameter))return false;
  const bool changed=p.lease.epoch!=0;
  if(changed) {
    const auto renewed=p.engine->renew(p.lease,binding,declaration.parameter);
    if(!renewed) {p.heartbeat={};return false;}
    if(renewed==1) {
      p.close();p.actions.clear();p.completed.clear();p.status.clear();p.heartbeat={};p.error.clear();p.subscriptions=256;return true;
    }
  }
  if(changed) {p.close();p.retire();p.actions.clear();p.completed.clear();p.status.clear();p.heartbeat={};p.error.clear();p.subscriptions=256;}
  if(p.lease.slot<0)p.lease=p.engine->reserve();
  if(p.engine->bind(p.lease,binding,declaration.parameter)) {
    auto header=p.engine->status(p.lease);
    if(!header.empty() && (header[22]!=1 || header[26]!=0 || header[24]<declaration.value["dataStates"].size() || header[25]<declaration.value["resources"].size()))
      throw Error("PROTOCOL_MISMATCH", "JSFX SDK capabilities do not match the native declaration");
  }
  return changed;
}
bool JsfxNativeHub::Node::available() const {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  return p.lease.epoch && p.error.empty() && Time::now()-p.heartbeat<std::chrono::milliseconds(500);
}
void JsfxNativeHub::Node::enqueue(uint64_t request,unsigned kind,unsigned id,std::vector<double> values,uint64_t owner) {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  if(p.actions.size()+p.completed.size()>=32)throw Error("QUEUE_LIMIT", "JSFX native control queue is full");
  p.actions.push_back({request,kind,id,std::move(values),owner});
}
bool JsfxNativeHub::Node::running(uint64_t request) const {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  return !p.status.empty() && p.status[9]==double(request) && p.status[8]>=2;
}
int JsfxNativeHub::Node::cancel(uint64_t request,bool allow_running) {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  for(auto it=p.actions.begin();it!=p.actions.end();++it)if(it->request==request) {
    const int result=it->sent?p.engine->cancel(p.lease,request,allow_running):1;
    if(result==1)p.actions.erase(it);
    return result;
  }
  return 0;
}
std::vector<JsfxNativeHub::Result> JsfxNativeHub::Node::results() {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  std::vector<Result> result;result.swap(p.completed);return result;
}
void JsfxNativeHub::Node::step() {
  auto& p=*impl_;std::vector<Engine::Packet> packets;std::vector<std::shared_ptr<Impl::Stream>> outputs;
  {
    std::lock_guard<std::mutex> lock(p.mutex);
    if(!p.lease.epoch || !p.error.empty())return;
    try {
      const auto now=Time::now();
      const bool poll_idle=now>=p.next_status;
      unsigned mask=0;for(const auto& out:p.outputs)mask|=1u<<out->index;
      if(mask!=p.subscriptions && p.engine->subscriptions(p.lease,mask))p.subscriptions=mask;
      if(now>=p.next_status) {
        auto header=p.engine->status(p.lease);
        if(!header.empty()) {
          if(header[2]!=double(p.lease.epoch))return;
          if(header[22]!=1 || header[26]!=0 || header[24]<declaration.value["dataStates"].size() || header[25]<declaration.value["resources"].size())
            throw Error("PROTOCOL_MISMATCH", "JSFX SDK capabilities do not match the native declaration");
          if((p.status.empty() && header[3]>0) || (!p.status.empty() && p.status[3]!=header[3]))p.heartbeat=now;
          p.status=std::move(header);
        }
        p.next_status=now+std::chrono::milliseconds(20);
      }
      if(!p.actions.empty()) {
        auto& action=p.actions.front();
        if(!action.sent)action.sent=p.engine->send(p.lease,action.request,action.kind,action.id,action.values,action.owner);
        Engine::Reply reply;
        if(action.sent && p.engine->reply(p.lease,reply)) {
          if(reply.request!=action.request)throw Error("PROTOCOL_MISMATCH", "Unexpected JSFX command acknowledgement");
          p.completed.push_back({reply.request,reply.code,std::move(reply.values)});p.actions.pop_front();
        }
      }
      outputs=p.outputs;
      for(unsigned i=0;i<16 && (!outputs.empty() || poll_idle);++i) {
        Engine::Packet packet;if(!p.engine->receive(p.lease,packet))break;
        ++p.received;if(!outputs.empty())packets.push_back(std::move(packet));
      }
    } catch(const Error& error) {p.error=error.code;++p.invalid;}
      catch(const std::exception&) {p.error="PROTOCOL_MISMATCH";++p.invalid;}
  }
  for(auto& out:outputs)for(const auto& packet:packets) {
    try {p.publish(*out,packet);}catch(const std::exception&) {out->pending_gap=true;}
  }
}
Json JsfxNativeHub::Node::open_stream(const std::string& id,const Json& options,int window,uint64_t document,const std::string& session) {
  if(!options.is_object())throw Error("INVALID_ARGUMENT", "Expected JSFX stream options");
  for(auto it=options.begin();it!=options.end();++it)if(it.key()!="updateRate")throw Error("INVALID_ARGUMENT", "Unknown JSFX stream option");
  if(!available())throw Error("DSP_UNAVAILABLE", "The JSFX audio processor is not scheduled");
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  const auto& list=declaration.value["streams"];
  auto found=std::find_if(list.begin(),list.end(),[&](const Json& item){return item["id"]==id;});
  if(found==list.end())throw Error("CAPABILITY_UNAVAILABLE", "The JSFX stream is not declared");
  const unsigned index=unsigned(found-list.begin());
  for(const auto& out:p.outputs)if(out->index==index && out->window==window && out->document==document)
    return {{"streamName",out->name},{"descriptor",out->descriptor}};
  if(p.outputs.size()>=8)throw Error("RESOURCE_LIMIT", "Too many JSFX stream consumers");
  auto out=std::make_shared<Impl::Stream>();out->index=index;out->window=window;out->document=document;
  out->channels=(*found)["channels"];out->rate=unsigned(p.status[5]);
  out->kind=kind_of((*found)["kind"]);out->fft=found->value("fftSize",2048u);out->points=std::min(out->fft,found->value("points",512u));
  const auto rate=options.value("updateRate",found->value("updateRate",Json(30)));
  if(!rate.is_number() || !std::isfinite(rate.get<double>()) || rate<1 || rate>120)throw Error("INVALID_ARGUMENT", "JSFX update rate must be in [1,120]");
  out->update_rate=rate.get<double>();out->name="jsfx-"+external_token();
  ReaWeb_StreamDesc desc{};desc.size=sizeof(desc);desc.abi_version=REAWEB_STREAM_ABI;
  desc.channels=out->channels;desc.sample_rate=out->rate;desc.capacity=8;desc.update_rate=out->update_rate;
  desc.format=out->kind>=REAWEB_BINARY?REAWEB_BYTES:REAWEB_FLOAT32;
  desc.block_frames=4096/out->channels;desc.fft_size=out->fft;
  desc.max_bytes=out->kind==REAWEB_SPECTRUM?(out->fft/2+1)*out->channels*4:
    out->kind==REAWEB_WAVEFORM?out->points*out->channels*2*4:
    out->kind==REAWEB_METER?uint32_t(AudioAnalysis::meter_size(out->channels)*4):
    out->kind>=REAWEB_BINARY?4096:desc.block_frames*out->channels*4;
  const auto source=(*found)["source"].get<std::string>();desc.source=source.c_str();
  out->descriptor=*found;
  out->descriptor.update({{"sessionId",session},{"layoutVersion",2},{"sampleRate",out->rate},
    {"clock","dspSamples"},{"fftSize",out->fft},{"hop","packetAligned"},{"points",out->points},
    {"units",out->kind==REAWEB_SPECTRUM?"linearAmplitude":"float32"},
    {"limits",{{"maxPacketValues",4096},{"updateRate",out->update_rate}}}});
  const auto result=p.streams.create_jsfx(out->kind,out->name.c_str(),&desc,&out->handle,window,document,out->descriptor);
  if(result!=REAWEB_OK)throw Error(StreamHub::code(result), "Cannot create the JSFX stream");
  p.outputs.push_back(out);return {{"streamName",out->name},{"descriptor",out->descriptor}};
}
void JsfxNativeHub::Node::close_window(int window) {auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);p.close(window);}
std::shared_ptr<JsfxTransfer> JsfxNativeHub::Node::create_transfer(size_t total,bool upload) {
  return std::make_shared<JsfxTransfer>(impl_->budget,external_token(),total,upload);
}
Json JsfxNativeHub::Node::attach_transfer(std::shared_ptr<JsfxTransfer> transfer,int window,uint64_t document,const std::string& origin) {
  return impl_->streams.attach_jsfx_resource(std::move(transfer),window,document,origin);
}
std::vector<double> JsfxNativeHub::Node::header() const {auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);return p.status;}
Json JsfxNativeHub::Node::diagnostics() const {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  return {{"receivedPackets",std::to_string(p.received)},{"invalidPackets",std::to_string(p.invalid)},
    {"queued",p.actions.size()},{"streams",p.outputs.size()},{"lastError",p.error},
    {"droppedPackets",p.status.empty()?"0":std::to_string(uint64_t(p.status[18]))}};
}
struct JsfxNativeHub::Impl {
  Host& host;StreamHub& streams;
  std::mutex mutex;
  std::vector<std::shared_ptr<Node>> nodes;
  std::map<std::pair<std::string,unsigned>,std::weak_ptr<Engine>> engines;
  std::shared_ptr<Retired> retired=std::make_shared<Retired>();
  std::shared_ptr<JsfxTransfer::Budget> budget=std::make_shared<JsfxTransfer::Budget>();
  std::atomic<bool> stopping{false};std::thread worker;
  Impl(Host& h,StreamHub& s):host(h),streams(s),worker([this] {
    while(!stopping) {
      std::vector<std::shared_ptr<Node>> current;
      {std::lock_guard<std::mutex> lock(mutex);current=nodes;}
      for(auto& node:current)node->step();
      current.clear();
      retired->collect();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }){}
  ~Impl() {stopping=true;if(worker.joinable())worker.join();nodes.clear();retired->collect();}
};
JsfxNativeHub::JsfxNativeHub(Host& host,StreamHub& streams):impl_(new Impl(host,streams)){}
JsfxNativeHub::~JsfxNativeHub()=default;
std::shared_ptr<JsfxNativeHub::Node> JsfxNativeHub::create(const JsfxNativeDeclaration& declaration,JsfxBinding& binding) {
  auto& p=*impl_;collect();std::lock_guard<std::mutex> lock(p.mutex);
  const auto key=std::make_pair(declaration.gmem,declaration.base);
  auto engine=p.engines[key].lock();
  if(!engine) {engine=std::make_shared<Engine>(p.host,declaration.gmem,declaration.base);p.engines[key]=engine;}
  if(p.nodes.size()>=128)throw Error("RESOURCE_LIMIT", "Too many active JSFX native instances");
  auto node=std::shared_ptr<Node>(new Node(declaration,engine,p.streams));node->impl_->retired=p.retired;
  node->impl_->budget=std::make_shared<JsfxTransfer::Budget>();
  node->impl_->budget->limit=32*1024*1024;node->impl_->budget->parent=p.budget;
  node->refresh(binding);p.nodes.push_back(node);return node;
}
void JsfxNativeHub::collect() {
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  p.nodes.erase(std::remove_if(p.nodes.begin(),p.nodes.end(),[](const auto& node){return node.use_count()==1;}),p.nodes.end());
  for(auto it=p.engines.begin();it!=p.engines.end();)if(it->second.expired())it=p.engines.erase(it);else ++it;
}
}
