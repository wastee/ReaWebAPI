// Opt-in audio observer for JSFX Web UI A/B tests. Does not link ReaWebAPI.
#include <reaper_plugin.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
namespace reaweb { using Json=nlohmann::json; }
#include "jsfx_audio_measurement.hpp"
namespace {
using Json=reaweb::Json;
reaper_plugin_info_t* host=nullptr;
std::filesystem::path root;
JsfxAudioMeasurement measurement;
std::string last;
bool quitting=false;
template<class T> T api(const char* name) {
  auto value=reinterpret_cast<T>(host->GetFunc(name));
  if(!value)throw std::runtime_error(std::string("Missing API: ")+name);
  return value;
}
Json device() {
  Json out;
  for(auto key:{"MODE","IDENT_IN","IDENT_OUT","BSIZE","SRATE"}) {
    char value[512]{};
    out[key]=api<bool(*)(const char*,char*,int)>("GetAudioDeviceInfo")(key,value,sizeof(value))?Json(value):Json(nullptr);
  }
  int input=0,output=0;
  api<void(*)(int*,int*)>("GetInputOutputLatency")(&input,&output);
  out["inputLatencySamples"]=input;out["outputLatencySamples"]=output;
  out["running"]=api<int(*)()>("Audio_IsRunning")();
  out["position"]=api<double(*)()>("GetPlayPosition")();
  return out;
}
Json command(const Json& c) {
  const auto op=c.at("op").get<std::string>();
  if(op=="device")return device();
  if(op=="setup") {
    if(api<int(*)(void*)>("CountTracks")(nullptr))throw std::runtime_error("Requires an empty disposable project");
    const int count=c.value("count",0);
    if(count<0||count>32)throw std::runtime_error("Invalid track count");
    for(int i=0;i<count;++i) {
      api<void(*)(int,bool)>("InsertTrackAtIndex")(i,false);
      auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,i);
      api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"D_VOL",0);
      api<bool(*)(void*,const char*,double)>("SetMediaTrackInfo_Value")(track,"I_PERFFLAGS",3);
      const auto fixture=c.value("fixture",std::string("rwa-audio-plain"));
      if(api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,fixture.c_str(),false,-1)<0)
        throw std::runtime_error("Fixture not found");
    }
    api<void(*)()>("OnPlayButton")();return device();
  }
  if(op=="begin") {measurement.begin(host);return device();}
  if(op=="end") {
    auto out=measurement.end();out["device"]=device();
    size_t gaps50=0,gaps500=0;
    for(size_t i=0;i<measurement.count;++i) {
      gaps50+=measurement.samples[i].interval_ms>50;
      gaps500+=measurement.samples[i].interval_ms>500;
    }
    out["gapsOver50ms"]=gaps50;out["gapsOver500ms"]=gaps500;return out;
  }
  if(op=="quit") {
    measurement.close();api<void(*)()>("OnStopButton")();
    for(int i=0;;++i) {
      auto project=api<void*(*)(int,char*,int)>("EnumProjects")(i,nullptr,0);
      if(!project)break;
      api<void(*)(void*,const char*,int)>("Main_SaveProjectEx")(project,(root/("audio-exit-"+std::to_string(i)+".rpp")).u8string().c_str(),8);
    }
    quitting=true;return true;
  }
  throw std::runtime_error("Unknown audio probe operation");
}
void tick() {
  try {
    if(quitting){quitting=false;api<void(*)(int,int)>("Main_OnCommand")(40004,0);return;}
    std::ifstream file(root/"audio-command.json");if(!file)return;
    auto c=Json::parse(file);const auto id=c.at("id").get<std::string>();if(id==last)return;last=id;
    Json out{{"id",id}};
    try{out["result"]=command(c);}catch(const std::exception& error){out["error"]=error.what();}
    std::ofstream(root/"audio-result.json")<<out.dump(2);
  }catch(...){}
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec) {
  if(!rec){measurement.close();return 0;}
  host=rec;root=std::filesystem::u8path(api<const char*(*)()>("GetResourcePath")());
  if(!std::filesystem::is_regular_file(root/"enable-audio-probe"))return 0;
  host->Register("timer",reinterpret_cast<void*>(tick));return 1;
}
