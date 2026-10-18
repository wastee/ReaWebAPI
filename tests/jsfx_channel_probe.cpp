// Manual P0 probe. Load only into an empty disposable REAPER profile.
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <reaper_plugin.h>
using J=nlohmann::json;
static reaper_plugin_info_t* host;
static J result;
static int ticks;
static void* probeTrack;
template<class T> T api(const char* name){return reinterpret_cast<T>(host->GetFunc(name));}
static double add(void*, double* a, double* b){return *a+*b;}
static void run(){
 ++ticks;
 if(ticks!=10 && ticks!=20)return;
 const auto root=std::filesystem::u8path(api<const char*(*)()>("GetResourcePath")());
 if(ticks==10){
 if(api<int(*)(void*)>("CountTracks")(nullptr)!=0){
  std::ofstream(root/"channel-probe.json")<<"{\"error\":\"Probe requires an empty project\"}";
  host->Register("-timer",reinterpret_cast<void*>(run));return;
 }
 for(const char* n:{"eel_gmem_attach","NSEEL_VM_alloc","NSEEL_VM_SetGRAM","NSEEL_VM_regvar","NSEEL_code_compile_ex","NSEEL_code_execute","NSEEL_code_free","NSEEL_VM_free","NSEEL_addfunc_ret_type","NSEEL_PProc_THIS","NSEEL_HOSTSTUB_EnterMutex","NSEEL_HOSTSTUB_LeaveMutex"})result["exports"][n]=host->GetFunc(n)!=nullptr;
 auto alloc=api<void*(*)()>("NSEEL_VM_alloc");
 auto reg=api<double*(*)(void*,const char*)>("NSEEL_VM_regvar");
 auto compile=api<void*(*)(void*,const char*,int,int)>("NSEEL_code_compile_ex");
 auto execute=api<void(*)(void*)>("NSEEL_code_execute");
 auto freecode=api<void(*)(void*)>("NSEEL_code_free");
 auto freevm=api<void(*)(void*)>("NSEEL_VM_free");
 auto registerfn=api<void(*)(const char*,int,int,void*,void*,void*)>("NSEEL_addfunc_ret_type");
 if(alloc&&reg&&compile&&execute&&freecode&&freevm&&registerfn){
  registerfn("reaweb_probe_add",2,1,host->GetFunc("NSEEL_PProc_THIS"),reinterpret_cast<void*>(add),nullptr);
  auto vm=alloc();auto out=reg(vm,"result");
  auto code=compile(vm,"result=reaweb_probe_add(2,3);",0,0);
  result["hostVmCompiled"]=code!=nullptr;
  if(code){execute(code);result["hostVmResult"]=*out;freecode(code);}freevm(vm);
 }
 api<void(*)(int,bool)>("InsertTrackAtIndex")(0,false);
 auto track=api<void*(*)(void*,int)>("GetTrack")(nullptr,0);
 probeTrack=track;
 for(const char* n:{"probe-control","probe-native-call","probe-gmem"}){
  int fx=api<int(*)(void*,const char*,bool,int)>("TrackFX_AddByName")(track,n,false,-1);
  result[n]["index"]=fx;
  if(fx>=0){api<bool(*)(void*,int,int,double)>("TrackFX_SetParam")(track,fx,0,1);double lo=0,hi=0;result[n]["value"]=api<double(*)(void*,int,int,double*,double*)>("TrackFX_GetParam")(track,fx,1,&lo,&hi);}
 }
 return;
 }
 auto track=probeTrack;
 for(int fx=0;fx<3;++fx){
  double lo=0,hi=0;
  result[fx==0?"probe-control":fx==1?"probe-native-call":"probe-gmem"]["settledValue"]=api<double(*)(void*,int,int,double*,double*)>("TrackFX_GetParam")(track,fx,1,&lo,&hi);
 }
 auto alloc=api<void*(*)()>("NSEEL_VM_alloc");
 auto reg=api<double*(*)(void*,const char*)>("NSEEL_VM_regvar");
 auto compile=api<void*(*)(void*,const char*,int,int)>("NSEEL_code_compile_ex");
 auto execute=api<void(*)(void*)>("NSEEL_code_execute");
 auto freecode=api<void(*)(void*)>("NSEEL_code_free");
 auto freevm=api<void(*)(void*)>("NSEEL_VM_free");
 auto attach=api<void**(*)(const char*,bool)>("eel_gmem_attach");
 auto setgram=api<void(*)(void*,void**)>("NSEEL_VM_SetGRAM");
 if(attach&&setgram&&alloc&&reg&&compile&&execute&&freecode&&freevm){
  auto vm=alloc();setgram(vm,attach("ReaWebAPI_P0_Probe",true));auto out=reg(vm,"result");
  auto code=compile(vm,"result=gmem[0];gmem[1]=91;",0,0);
  result["sharedVmCompiled"]=code!=nullptr;
  if(code){execute(code);result["sharedVmRead"]=*out;freecode(code);}setgram(vm,nullptr);freevm(vm);attach("ReaWebAPI_P0_Probe",false);
  api<bool(*)(void*,int,int,double)>("TrackFX_SetParam")(track,2,0,2);
  double lo=0,hi=0;result["jsfxSharedRead"]=api<double(*)(void*,int,int,double*,double*)>("TrackFX_GetParam")(track,2,1,&lo,&hi);
 }
 result["version"]=api<const char*(*)()>("GetAppVersion")();
 std::ofstream(root/"channel-probe.json")<<result.dump(2);
 host->Register("-timer",reinterpret_cast<void*>(run));
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec){
 if(!rec)return 0;host=rec;const auto root=std::filesystem::u8path(api<const char*(*)()>("GetResourcePath")());
 if(!std::filesystem::exists(root/"enable-channel-probe"))return 0;
 host->Register("timer",reinterpret_cast<void*>(run));return 1;
}
