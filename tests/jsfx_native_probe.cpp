// Opt-in acceptance driver. Never installed with the runtime.
#include "runtime/jsfx_channel.hpp"
#include "runtime/jsfx_control.hpp"
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <reaper_plugin.h>
#include "jsfx_audio_measurement.hpp"
using namespace reaweb;
namespace {
reaper_plugin_info_t* plugin;
fs::path root;
Host host;
std::unique_ptr<JsfxChannelEngine> engine;
StreamHub streams;
std::unique_ptr<JsfxNativeHub> native_hub;
std::vector<std::unique_ptr<JsfxControl>> controls;
std::vector<std::string> sessions;
bool native_undo=false;
bool quit_requested=false;
JsfxAudioMeasurement audio_measurement;
std::vector<double> telemetry_latency,sdk_block_ms;
bool measure_latency=false,backpressure=false;
double (*precise_clock)()=nullptr;
struct Entry {
  std::unique_ptr<JsfxBinding> binding;
  JsfxChannelEngine::Lease lease;
  uint64_t frames=0, errors=0, last_sequence=0;
  uint64_t rebinds=0, stale_rejections=0;
  double maximum=0;
};
std::vector<Entry> entries;
std::thread worker;
std::atomic<bool> stopped{true};
std::string last;
template<class T> T api(const char* name) {
  auto result=reinterpret_cast<T>(plugin->GetFunc(name));
  if (!result) throw std::runtime_error(std::string("Missing API: ")+name);
  return result;
}
void stop() { stopped=true; if(worker.joinable())worker.join(); }
void start() {
  stop();if(!engine || backpressure)return;stopped=false;
  worker=std::thread([] {
    while(!stopped) {
      for(auto& entry:entries) for(int n=0;n<16;++n) {
        try {
          JsfxChannelEngine::Packet packet;
          if(!engine->receive(entry.lease,packet))break;
          if(packet.id==1) {
            if(measure_latency && packet.values.size()==2 && telemetry_latency.size()<262144) {
              telemetry_latency.push_back((precise_clock()-packet.values[0])*1000);sdk_block_ms.push_back(packet.values[1]);
            }
            continue;
          }
          ++entry.frames;
          if(packet.sequence<=entry.last_sequence || packet.channels!=2 || packet.epoch!=entry.lease.epoch)++entry.errors;
          entry.last_sequence=packet.sequence;
          for(size_t i=0;i<packet.values.size();++i) {
            const double expected=std::sin((packet.first_sample+i/2)*2*3.14159265358979323846*440/packet.sample_rate);
            const double error=std::abs(packet.values[i]-expected);
            entry.maximum=std::max(entry.maximum,error);
            if(error>1e-7)++entry.errors;
          }
        } catch(const Error& error) {
          if(error.code=="STALE_SESSION")++entry.stale_rejections;
          else ++entry.errors;
          break;
        } catch(...) { ++entry.errors; break; }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
}
bool bind_wait(Entry& entry) {
  bool bound=false;
  for(int i=0;i<2000&&!bound;++i) { bound=engine->bind(entry.lease,*entry.binding,1); if(!bound)std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  return bound;
}
void add(JsfxTarget target,int fx) {
  Entry entry; entry.binding=std::make_unique<JsfxBinding>(host,target,fx); entry.lease=engine->reserve();
  const bool bound=bind_wait(entry);
  if(!bound) {
    const auto diagnostic=Json{{"parameter",entry.binding->parameter(1,true)},{"header",engine->status(entry.lease)},{"slot",entry.lease.slot},{"token",entry.lease.token}};
    engine->release(entry.lease);throw std::runtime_error("Handshake rejected: "+diagnostic.dump());
  }
  engine->subscriptions(entry.lease,255);
  entries.push_back(std::move(entry));
}
Json summary() {
  Json out=Json::array();
  for(auto& entry:entries) {
    Json state{{"slot",entry.lease.slot},{"token",entry.lease.token},{"epoch",entry.lease.epoch},
      {"valid",false},{"frames",entry.frames},{"errors",entry.errors},{"maximumError",entry.maximum},{"rebinds",entry.rebinds},{"staleRejections",entry.stale_rejections}};
    try{state["valid"]=engine->valid(entry.lease,*entry.binding,1);}catch(const Error& error){state["unavailable"]=error.code;}
    out.push_back(std::move(state));
  }
  return out;
}
Json command(const Json& c) {
  const std::string op=c.at("op");
  stop();
  if(op=="measureBegin") {
    const auto mode=c.value("mode",std::string("active"));
    backpressure=mode=="backpressure";measure_latency=mode=="active";
    telemetry_latency.clear();telemetry_latency.reserve(262144);
    sdk_block_ms.clear();sdk_block_ms.reserve(262144);
    precise_clock=api<double(*)()>("time_precise");
    for(auto& entry:entries) {
      entry.binding->set(2,mode=="baseline"?0:1,false);
      engine->subscriptions(entry.lease,mode=="idle"||mode=="baseline"?0:255);
    }
    audio_measurement.begin(plugin);start();return true;
  }
  if(op=="measureEnd") {
    auto result=audio_measurement.end();result["publicationToNativeMs"]=JsfxAudioMeasurement::distribution(telemetry_latency);
    result["sdkBlockMs"]=JsfxAudioMeasurement::distribution(sdk_block_ms);
    int input=0,output=0;api<void(*)(int*,int*)>("GetInputOutputLatency")(&input,&output);
    result["inputLatencySamples"]=input;result["outputLatencySamples"]=output;
    backpressure=false;measure_latency=false;start();return result;
  }
  if(op=="quietFixture") {
    if(api<int(*)(void*)>("CountTracks")(nullptr))throw std::runtime_error("Requires an empty disposable project");
    api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);
    api<void(*)(int,bool)>("InsertTrackAtIndex")(0,false);
    auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"D_VOL",0);
    const int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",false,-1);
    api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"Add quiet fixture",-1);
    api<void(*)()>("OnPlayButton")();return fx;
  }
  if(op=="quietEdit") {
    api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);
    auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"D_PAN",c.value("value",0.25));
    api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"Quiet fixture pan",1);
    start();return true;
  }
  if(op=="save") {
    api<void(*)(void*,const char*,int)>("Main_SaveProjectEx")(nullptr,(root/"native-roundtrip.rpp").u8string().c_str(),8);
    start();return fs::is_regular_file(root/"native-roundtrip.rpp");
  }
  if(op=="history") {
    const auto undo=api<const char*(*)(void*)>("Undo_CanUndo2")(nullptr);
    const auto redo=api<const char*(*)(void*)>("Undo_CanRedo2")(nullptr);
    start();return {{"undo",undo?undo:""},{"redo",redo?redo:""},
      {"dirty",api<int(*)(void*)>("IsProjectDirty")(nullptr)},
      {"entries",api<int(*)(void*)>("Undo_GetNumEntries")(nullptr)},
      {"current",api<int(*)(void*)>("Undo_GetCurEntry")(nullptr)}};
  }
  if(op=="bootstrapFixture" || op=="bootstrap") {
    controls.clear();sessions.clear();native_hub.reset();
    if(engine)for(auto& entry:entries)engine->release(entry.lease);
    entries.clear();engine.reset();
    auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    if(!track)throw std::runtime_error("A disposable fixture track is required");
    if(op=="bootstrapFixture") {
      api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);
      const int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",false,-1);
      api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"Add bootstrap fixture",2);return fx;
    }
    engine=std::make_unique<JsfxChannelEngine>(host,"ReaWebAPI_Native_Probe_v1",0);
    if(c.contains("flags"))api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);
    add(JsfxTarget{track},c.at("fx"));
    if(c.contains("flags"))api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"JSFX bootstrap probe",c.at("flags"));
    start();return entries.front().binding->parameter(1);
  }
  if(op=="quit") {
    audio_measurement.close();api<void(*)()>("OnStopButton")();
    controls.clear();sessions.clear();native_hub.reset();entries.clear();engine.reset();
    for(int i=0;;++i) {
      auto project=api<void*(*)(int,char*,int)>("EnumProjects")(i,nullptr,0);
      if(!project)break;
      api<void(*)(void*,const char*,int)>("Main_SaveProjectEx")(project,(root/("native-exit-"+std::to_string(i)+".rpp")).u8string().c_str(),8);
    }
    quit_requested=true;return true;
  }
  if(op=="device" || op=="audioInit") {
    if(op=="audioInit")api<void(*)()>("Audio_Init")();
    Json result{{"running",api<int(*)()>("Audio_IsRunning")()},
      {"playState",api<int(*)()>("GetPlayState")()},
      {"position",api<double(*)()>("GetPlayPosition")()}};
    for(const auto key:{"MODE","IDENT_IN","IDENT_OUT","BSIZE","SRATE"}) {
      char value[512]{};
      result[key]=api<bool(*)(const char*,char*,int)>("GetAudioDeviceInfo")(key,value,sizeof(value))?Json(value):Json(nullptr);
    }
    if(engine)for(auto& entry:entries)result["headers"].push_back(engine->status(entry.lease));
    start();return result;
  }
  if(op=="setup") {
    if(api<int(*)(void*)>("CountTracks")(nullptr))throw std::runtime_error("Requires an empty disposable project");
    const int count=c.value("count",16);
    if(count<1||count>24)throw std::runtime_error("Invalid count");
    api<void(*)()>("OnPlayButton")();
    for(int i=0;i<count;++i) {
      api<void(*)(int,bool)>("InsertTrackAtIndex")(i,false);
      auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,i);
      api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"D_VOL",0);
      api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"I_PERFFLAGS",3);
      const int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",false,-1);
      if(fx<0)throw std::runtime_error("Fixture not found");
      if(!engine)engine=std::make_unique<JsfxChannelEngine>(host,"ReaWebAPI_Native_Probe_v1",0);
      add(JsfxTarget{track},fx);
    }
    auto result=summary();start();return result;
  }
  if(op=="hubSetup") {
    if(!controls.empty())throw std::runtime_error("Hub already initialized");
    for(auto& entry:entries)if(!engine->release(entry.lease))throw std::runtime_error("Probe lease still busy");
    engine.reset();native_hub=std::make_unique<JsfxNativeHub>(host,streams);
    const Json scalar{{"type","number"},{"minimum",0},{"maximum",1}};
    const JsfxNativeDeclaration declaration({{"protocolVersion",1},{"gmem","ReaWebAPI_Native_Probe_v1"},{"base",0},{"transportParam",1},
      {"dataStates",Json::array({{{"id","curve"},{"schemaVersion",1},{"schema",{{"type","array"},{"minItems",4},{"maxItems",4},{"items",scalar}}},{"persistent",true},{"undo",true}}})},
      {"commands",Json::array({{{"id","sum"},{"args",{{"type","object"},{"properties",{{"a",scalar},{"b",scalar}}}}},{"result",{{"type","number"},{"minimum",0},{"maximum",2}}}},
        {{"id","delayed"},{"args",{{"type","integer"},{"minimum",1},{"maximum",1000}}},{"result",scalar},{"cancellable",true},{"maxExecutionMs",50},{"undo",true}},
        {{"id","late"},{"args",{{"type","integer"},{"minimum",1},{"maximum",1000}}},{"result",scalar},{"maxExecutionMs",50},{"undo",true}},
        {{"id","note"},{"args",{{"type","object"},{"properties",{{"channel",{{"type","integer"},{"minimum",0},{"maximum",15}}},{"note",{{"type","integer"},{"minimum",0},{"maximum",127}}},{"velocity",{{"type","integer"},{"minimum",0},{"maximum",127}}}}}}},{"result",scalar},{"continuous",true},{"cancellable",true}},
        {{"id","notes"},{"args",{{"type","object"},{"properties",Json::object()}}},{"result",{{"type","integer"},{"minimum",0},{"maximum",128}}}}})},
      {"resources",Json::array({{{"id","asset"},{"format","bytes"},{"maxBytes",32768},{"persistent",true},{"undo",true}},
        {{"id","table"},{"format","float32"},{"maxBytes",32768},{"persistent",true},{"undo",true}}})},
      {"streams",Json::array({{{"id","output"},{"kind",c.value("streamKind",std::string("spectrum"))},{"source","output"},{"channels",2},{"fftSize",2048}}})}});
    for(auto& entry:entries) {
      controls.push_back(std::make_unique<JsfxControl>(native_hub->create(declaration,*entry.binding)));
      sessions.push_back("probe-"+std::to_string(sessions.size()));
    }
    return controls.size();
  }
  if(op=="hubStatus") {
    Json result=Json::array();for(size_t i=0;i<controls.size();++i)
      result.push_back({{"capabilities",controls[i]->capabilities(sessions[i])},{"diagnostics",controls[i]->diagnostics()}});
    return result;
  }
  if(op=="hubCall") {
    const auto index=c.value("index",0u);
    auto result=controls.at(index)->call(c.at("method"),c.value("payload",Json::object()),{c.value("window",7),c.value("document",uint64_t(1)),sessions.at(index),"http://jsfx-probe.test"});
    if(c.at("method")=="openStream")result["transport"]=streams.attach(result["streamName"],7,1,"http://jsfx-probe.test");
    return result;
  }
  if(op=="hubClose") {
    controls.clear();sessions.clear();native_hub.reset();entries.clear();return true;
  }
  if(op=="midiObserver") {
    auto* track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    return api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-midi-observer",false,-1);
  }
  if(op=="midiEvents") {
    auto* track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    Json result=Json::array();double lo=0,hi=0;
    for(int i=0;i<4;++i)result.push_back(api<double(*)(void*,int,int,double*,double*)>("TrackFX_GetParam")(track,c.at("fx"),i,&lo,&hi));
    return result;
  }
  if(op=="undo"||op=="redo") {
    const auto result=api<int(*)(void*)>(op=="undo"?"Undo_DoUndo2":"Undo_DoRedo2")(nullptr);start();return result;
  }
  if(!engine)throw std::runtime_error("Run setup first");
  if(op=="status") { auto result=summary();start();return result; }
  if(op=="play") { api<void(*)()>("OnPlayButton")();start();return true; }
  if(op=="undoBegin") {api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);start();return true;}
  if(op=="undoEnd") {
    auto& binding=*entries.at(c.value("index",0u)).binding;
    const auto notify=c.value("notify",std::string());
    if(notify=="parameter")binding.set(1,binding.parameter(1)["value"],false);
    else if(notify=="dirty")api<void(*)(void*)>("MarkProjectDirty")(nullptr);
    else if(notify=="track")api<void(*)(void*,const char*,int,int)>("Undo_OnStateChangeEx2")(nullptr,"JSFX native state test",2,0);
    api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"JSFX native state test",c.value("flags",2));start();return true;
  }
  if(op=="load") {
    for(auto& entry:entries)engine->release(entry.lease);
    entries.clear();
    api<void(*)(const char*)>("Main_openProject")(("noprompt:"+(root/"native-roundtrip.rpp").u8string()).c_str());
    const int count=api<int(*)(void*)>("CountTracks")(nullptr);
    for(int i=0;i<count;++i) {
      auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,i);
      add(JsfxTarget{track},0);
    }
    auto result=summary();start();return result;
  }
  if(op=="seek") { api<void(*)(void*,double,bool,bool)>("SetEditCurPos2")(nullptr,c.at("position"),true,true);start();return true; }
  if(op=="enabled" || op=="offline") {
    auto& entry=entries.at(c.value("index",0));
    const auto target=entry.binding->target();int fx=-1;
    for(int index:target.indices(host)) {
      auto guid=target.api<Guid*(*)(void*,int)>(host,"GetFXGUID")(target.object(),index);
      if(guid&&*guid==entry.binding->guid()){fx=index;break;}
    }
    if(fx<0)throw std::runtime_error("Offline fixture was deleted");
    target.api<void(*)(void*,int,bool)>(host,op=="enabled"?"SetEnabled":"SetOffline")(target.object(),fx,c.at("value"));
    start();return true;
  }
  if(op=="transportParameter")return entries.at(0).binding->parameter(1,true);
  if(op=="stop") { api<void(*)()>("OnStopButton")();return summary(); }
  if(op=="send") {
    auto& entry=entries.at(c.value("index",0));
    auto values=c.at("values").get<std::vector<double>>();
    auto result=engine->send(entry.lease,c.value("request",1),c.value("kind",1),c.value("capability",0),values);start();return result;
  }
  if(op=="reply") {
    JsfxChannelEngine::Reply reply;
    auto ready=engine->reply(entries.at(c.value("index",0)).lease,reply);
    start();return {{"ready",ready},{"request",reply.request},{"code",reply.code},{"values",reply.values}};
  }
  if(op=="copy") {
    auto& entry=entries.at(c.value("index",0));
    auto track=entry.binding->track();
    const int next=api<int(*)(void*)>("TrackFX_GetCount")(track);
    api<void(*)(void*,int,void*,int,bool)>("TrackFX_CopyToTrack")(track,entry.binding->resolve(),track,next,false);
    add(JsfxTarget{track},next);auto result=summary();start();return result;
  }
  if(op=="chain") {
    const auto kind=c.at("kind").get<std::string>();
    auto track=kind=="master"||kind=="monitor"?api<void*(*)(void*)>("GetMasterTrack")(nullptr):entries.at(0).binding->track();
    JsfxTarget target{track,nullptr,kind=="input"||kind=="monitor"};
    int fx=-1;
    if(kind=="take") {
      auto item=api<void*(*)(void*,double,double,bool*)>("CreateNewMIDIItemInProj")(track,0,600,nullptr);
      target.take=api<void*(*)(void*)>("GetActiveTake")(item);
      fx=api<int(*)(void*,const char*,int)>("TakeFX_AddByName")(target.take,"rwa-native-probe",-1);
    } else {
      if(kind=="input") {
        api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"I_RECARM",1);
        api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"I_RECMON",1);
      }
      fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",target.input,-1);
      if(target.input)fx|=0x1000000;
      if(kind=="container") {
        const int parent=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"Container",false,-1);
        const int count=api<int(*)(void*)>("TrackFX_GetCount")(track);
        api<void(*)(void*,int,void*,int,bool)>("TrackFX_CopyToTrack")(track,fx,track,0x2000000+parent+1+count+1,true);
        // Moving the earlier FX shifts the container's top-level index.
        int adjusted=parent-(fx<parent?1:0);
        const int depth=c.value("depth",2);
        if(depth<1||depth>2)throw std::runtime_error("Invalid test depth");
        if(depth==2) {
          const int outer=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"Container",false,-1);
          const int count2=api<int(*)(void*)>("TrackFX_GetCount")(track);
          api<void(*)(void*,int,void*,int,bool)>("TrackFX_CopyToTrack")(track,adjusted,track,0x2000000+outer+1+count2+1,true);
          adjusted=std::stoi(target.config(host,outer-1,"container_item.0"));
        }
        fx=std::stoi(target.config(host,adjusted,"container_item.0"));
      }
    }
    if(fx<0)throw std::runtime_error("Cannot create test chain FX");
    add(target,fx);auto result=summary();start();return result;
  }
  if(op=="replace") {
    const auto index=c.value("index",0);auto& entry=entries.at(index);auto track=entry.binding->track();
    api<bool(*)(void*,int)>("TrackFX_Delete")(track,entry.binding->resolve());
    bool rejected=false;try{engine->valid(entry.lease,*entry.binding,1);}catch(const Error& e){rejected=e.code=="FX_STALE";}
    bool released=engine->release(entry.lease);
    const int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",false,-1);
    const auto old=entry.lease;
    entry.binding=std::make_unique<JsfxBinding>(host,track,fx);entry.lease=engine->reserve();
    const bool bound=bind_wait(entry);
    entry.last_sequence=0;
    start();return {{"oldRejected",rejected},{"oldReleased",released},{"bound",bound},{"newToken",entry.lease.token!=old.token},{"newEpoch",entry.lease.epoch!=old.epoch}};
  }
  if(op=="undoDelete") {
    auto& entry=entries.at(c.value("index",0));const auto target=entry.binding->target();const auto old=entry.lease;
    api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);
    target.api<bool(*)(void*,int)>(host,"Delete")(target.object(),entry.binding->resolve());
    api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"JSFX channel deletion test",2);
    bool rejected=false;try{entry.binding->resolve();}catch(const Error& e){rejected=e.code=="FX_STALE";}
    const bool released=engine->release(old);
    api<int(*)(void*)>("Undo_DoUndo2")(nullptr);
    entry.lease=engine->reserve();const bool bound=bind_wait(entry);
    entry.last_sequence=0;start();
    return {{"oldRejected",rejected},{"oldReleased",released},{"bound",bound},{"newEpoch",entry.lease.epoch!=old.epoch},{"newToken",entry.lease.token!=old.token}};
  }
  if(op=="projects") {
    auto original=host.current_project();const auto previous=entries.front().lease;
    api<void(*)(int,int)>("Main_OnCommand")(40859,0);
    if(host.current_project()==original)throw std::runtime_error("New project tab was not created");
    bool rejected=false;try{entries.front().binding->resolve();}catch(const Error& e){rejected=e.code=="FX_STALE";}
    api<void(*)(int,bool)>("InsertTrackAtIndex")(0,false);
    auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
    api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"D_VOL",0);
    api<void(*)()>("OnPlayButton")();
    const int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,"rwa-native-probe",false,-1);
    add(JsfxTarget{track},fx);
    const auto other=entries.back().lease;
    const bool isolated=engine->valid(other,*entries.back().binding,1)&&other.token!=previous.token&&other.epoch!=previous.epoch;
    engine->release(other);entries.pop_back();
    api<void(*)(void*)>("SelectProjectInstance")(original);
    start();return {{"oldRejectedInOtherProject",rejected},{"otherProjectIsolated",isolated},{"returned",host.current_project()==original}};
  }
  if(op=="close") {
    api<void(*)()>("OnStopButton")();
    bool released=true;for(auto& entry:entries)released=engine->release(entry.lease)&&released;
    entries.clear();engine.reset();return {{"released",released}};
  }
  throw std::runtime_error("Unknown probe operation");
}
void tick() {
  try {
    if(quit_requested) {quit_requested=false;api<void(*)(int,int)>("Main_OnCommand")(40004,0);return;}
    for(size_t i=0;i<controls.size();++i) {
      if(controls[i]->tick(*entries[i].binding,[](const std::string&){
        if(native_undo)return false;api<void(*)(void*)>("Undo_BeginBlock2")(nullptr);native_undo=true;return true;
      },[]{if(native_undo)api<void(*)(void*,const char*,int)>("Undo_EndBlock2")(nullptr,"JSFX native control",2);native_undo=false;}))sessions[i]+="-next";
      controls[i]->events();
    }
    bool restart=false;
    if(engine)for(auto& entry:entries) {
      try {
        if(!engine->valid(entry.lease,*entry.binding,1)) {
          if(!restart){stop();restart=true;}
          if(!entry.lease.epoch) {
            if(engine->bind(entry.lease,*entry.binding,1)) {engine->subscriptions(entry.lease,255);entry.last_sequence=0;++entry.rebinds;}
            continue;
          }
          const auto renewed=engine->renew(entry.lease,*entry.binding,1);
          if(renewed==1) {entry.last_sequence=0;++entry.rebinds;continue;}
          if(!renewed)continue;
          if(!engine->release(entry.lease))continue;
          entry.lease=engine->reserve();
          if(engine->bind(entry.lease,*entry.binding,1)) {engine->subscriptions(entry.lease,255);entry.last_sequence=0;++entry.rebinds;}
        }
      }catch(const Error&) {} // Offline FX may not expose parameter metadata.
    }
    if(restart)start();
    std::ifstream file(root/"native-command.json");if(!file)return;
    auto c=Json::parse(file);const std::string id=c.at("id");if(id==last)return;last=id;
    Json result{{"id",id}};
    try{result["result"]=command(c);}catch(const std::exception& e){result["error"]=e.what();}
    std::ofstream(root/"native-result.json")<<result.dump(2,' ',false,Json::error_handler_t::replace);
  }catch(...){}
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec) {
  if(!rec){audio_measurement.close();stop();controls.clear();native_hub.reset();entries.clear();engine.reset();return 0;}
  plugin=rec;root=fs::u8path(api<const char*(*)()>("GetResourcePath")());
  if(!fs::exists(root/"enable-native-probe"))return 0;
  host.native_function=plugin->GetFunc;
  host.current_project=[](){return api<void*(*)(int,char*,int)>("EnumProjects")(-1,nullptr,0);};
  host.valid_track=[](void* p,void* t){return api<bool(*)(void*,void*,const char*)>("ValidatePtr2")(p,t,"MediaTrack*");};
  host.track_guid=[](void* t){return *api<Guid*(*)(void*)>("GetTrackGUID")(t);};
  rec->Register("timer",reinterpret_cast<void*>(tick));return 1;
}
