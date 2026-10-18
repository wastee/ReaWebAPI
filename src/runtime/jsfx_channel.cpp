#include "runtime/jsfx_channel.hpp"
#include "runtime/external_settings.hpp"
#include "jsfx_channel_source.hpp"
#include <cmath>
#include <fstream>
#include <regex>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace reaweb {
namespace {
// Versioned public export prefix from Cockos jsfx/jsfx_api.h, not SX_Instance layout.
struct EngineApi {
  unsigned version;
  void* (*create)(const char*, const char*, bool*);
  int (*process)(void*, double*, int, int, int, double, int, int, double, double, double, double, double, int);
  void* (*create_ui)(void*, void*, void*, void*);
  void* (*get_ui)(void*);
  void (*delete_ui)(void*);
  void (*destroy)(void*);
  const char* (*name)(void*);
  void (*load)(void*, const char*);
  const char* (*save)(void*, int*);
  int (*count)(void*);
  double (*get)(void*, int, double*, double*, double*);
  void (*param_name)(void*, int, char*, int);
  void (*set)(void*, int, double, int);
};
template<class T> T required(Host& host, const char* name) {
  auto fn = host.native_function ? reinterpret_cast<T>(host.native_function(name)) : nullptr;
  if (!fn) throw Error("UNSUPPORTED", std::string("JSFX channel requires host export: ") + name);
  return fn;
}
void replace(std::string& text, const std::string& key, const std::string& value) {
  size_t at = 0;
  while ((at = text.find(key, at)) != std::string::npos) { text.replace(at, key.size(), value); at += value.size(); }
}
uint64_t integer(double value, uint64_t maximum = 9007199254740991ULL) {
  if (!std::isfinite(value) || value < 0 || value > double(maximum) || std::floor(value) != value)
    throw Error("PROTOCOL_MISMATCH", "Invalid JSFX channel integer");
  return static_cast<uint64_t>(value);
}
std::mutex registry_mutex;
std::map<std::pair<std::string,unsigned>,const void*> registered_ranges;
}
struct JsfxChannelEngine::Impl {
  Host& host;
  std::string name;
  fs::path root;
  void* module = nullptr;
  EngineApi* engine = nullptr;
  void* helper = nullptr;
  void* vm = nullptr;
  void* read = nullptr;
  void* write = nullptr;
  void* read_one = nullptr;
  void* write_one = nullptr;
  double *offset = nullptr, *value = nullptr, *length = nullptr;
  std::vector<double*> local;
  void** (*attach)(const char*, bool) = nullptr;
  void (*gram)(void*, void**) = nullptr;
  void (*execute)(void*) = nullptr;
  void (*free_code)(void*) = nullptr;
  void (*free_vm)(void*) = nullptr;
  uint32_t dispatch = 0;
  unsigned stage = 0;
  bool attached = false;
  bool registered = false;
  std::string range_name;
  std::mutex mutex;
  explicit Impl(Host& h) : host(h) {}
  ~Impl() {
    if (helper) engine->destroy(helper);
    for (auto code : {read, write, read_one, write_one}) if (code && free_code) free_code(code);
    if (vm) { if (gram) gram(vm, nullptr); if (free_vm) free_vm(vm); }
    if (attached) attach(name.c_str(), false);
#ifdef _WIN32
    if (module) FreeLibrary(static_cast<HMODULE>(module));
#else
    if (module) dlclose(module);
#endif
    if (!root.empty()) { std::error_code ec; fs::remove(root/"Effects"/"bridge",ec); fs::remove(root/"Effects",ec); fs::remove(root,ec); }
    if (registered) { std::lock_guard<std::mutex> lock(registry_mutex); registered_ranges.erase({range_name,stage-2162688}); }
  }
  void put(unsigned at, double v) { *offset=stage+at; *value=v; execute(write_one); }
  double get(unsigned at) { *offset=stage+at; execute(read_one); return *value; }
  int call(unsigned operation, const Lease& lease) {
    put(0,operation); put(1,lease.slot); put(2,double(lease.token));put(4,double(lease.epoch));
    if (++dispatch >= 2147483647) dispatch=1;
    engine->set(helper,0,dispatch,0);
    return static_cast<int>(get(3));
  }
  void upload(const std::vector<double>& values) {
    for (size_t i=0;i<values.size();++i) *local[i]=values[i];
    *length=double(values.size()); execute(write);
  }
  std::vector<double> download(size_t size) {
    if (size>8192) throw Error("PROTOCOL_MISMATCH", "JSFX packet exceeds its reserved region");
    *length=double(size); execute(read);
    std::vector<double> result(size);
    for (size_t i=0;i<size;++i) result[i]=*local[i];
    return result;
  }
};
JsfxChannelEngine::JsfxChannelEngine(Host& host, const std::string& name, unsigned base) : impl_(new Impl(host)) {
  if (!std::regex_match(name,std::regex("[A-Za-z][A-Za-z0-9_.-]{0,127}")) || base>4194304)
    throw Error("INVALID_ARGUMENT", "Invalid declared JSFX gmem namespace or reserved range");
  auto& p=*impl_; p.name=name; p.stage=base+2162688;
  p.range_name=name;
  for(auto& c:p.range_name)if(c>='A'&&c<='Z')c+=32;
  {
    std::lock_guard<std::mutex> lock(registry_mutex);
    for(const auto& range:registered_ranges)if(range.first.first==p.range_name &&
        base<range.first.second+2170944 && range.first.second<base+2170944)
      throw Error("RESOURCE_LIMIT", "A JSFX native bridge already owns this shared memory range");
    registered_ranges.emplace(std::make_pair(p.range_name,base),&p);p.registered=true;
  }
#ifdef _WIN32
  HMODULE module=nullptr;
  if (GetModuleHandleExW(0,L"jsfx.dll",&module)) { p.module=module; p.engine=reinterpret_cast<EngineApi*>(GetProcAddress(module,"JesusonicAPI")); }
#else
  std::string path;
#ifdef __APPLE__
  for (uint32_t i=0;i<_dyld_image_count();++i) {
    const auto candidate=fs::path(_dyld_get_image_name(i));
    if (candidate.filename()=="jsfx.dylib") { path=candidate.string(); break; }
  }
#else
  path=(fs::u8path(required<const char*(*)()>(host,"GetExePath")())/"Plugins"/"jsfx.so").string();
#endif
  if (!path.empty()) p.module=dlopen(path.c_str(),RTLD_NOW|RTLD_NOLOAD);
  if (p.module) p.engine=reinterpret_cast<EngineApi*>(dlsym(p.module,"JesusonicAPI"));
#endif
  if (!p.engine || p.engine->version!=0xf0f01091 || !p.engine->create || !p.engine->destroy || !p.engine->set || !p.engine->get)
    throw Error("UNSUPPORTED", "This JSFX engine ABI is not supported");
  p.attach=required<void**(*)(const char*,bool)>(host,"eel_gmem_attach");
  p.gram=required<void(*)(void*,void**)>(host,"NSEEL_VM_SetGRAM");
  p.execute=required<void(*)(void*)>(host,"NSEEL_code_execute");
  p.free_code=required<void(*)(void*)>(host,"NSEEL_code_free");
  p.free_vm=required<void(*)(void*)>(host,"NSEEL_VM_free");
  p.vm=required<void*(*)()>(host,"NSEEL_VM_alloc")();
  if (!p.vm) throw Error("RESOURCE_LIMIT", "Cannot allocate JSFX staging VM");
  auto gram=p.attach(name.c_str(),true);
  if (!gram) throw Error("RESOURCE_LIMIT", "Cannot attach JSFX shared segment");
  p.attached=true; p.gram(p.vm,gram);
  auto reg=required<double*(*)(void*,const char*)>(host,"NSEEL_VM_regvar");
  p.offset=reg(p.vm,"offset"); p.value=reg(p.vm,"value"); p.length=reg(p.vm,"length");
  auto compile=required<void*(*)(void*,const char*,int,int)>(host,"NSEEL_code_compile_ex");
  p.read_one=compile(p.vm,"value=gmem[offset];",0,0);
  p.write_one=compile(p.vm,"gmem[offset]=value;",0,0);
  std::string read,write;
  // Registered variables are a supported host EEL interface, with no RAM-layout access.
  for (int i=0;i<8192;++i) {
    const auto v="v"+std::to_string(i), address=std::to_string(p.stage+64+i);
    p.local.push_back(reg(p.vm,v.c_str()));
    if (i%64==0) { const auto gate="length>"+std::to_string(i)+"?("; read+=gate; write+=gate; }
    read+=v+"=gmem["+address+"];";
    write+="gmem["+address+"]="+v+";";
    if (i%64==63) { read+=");"; write+=");"; }
  }
  p.read=compile(p.vm,read.c_str(),0,0); p.write=compile(p.vm,write.c_str(),0,0);
  if (!p.read || !p.write || !p.read_one || !p.write_one || !p.offset || !p.value || !p.length ||
      std::find(p.local.begin(),p.local.end(),nullptr)!=p.local.end()) throw Error("UNSUPPORTED", "Cannot compile JSFX staging program");
  p.root=fs::temp_directory_path()/fs::u8path("reaweb-jsfx-"+external_token());
  fs::create_directories(p.root/"Effects");
  std::string source=jsfx_bridge_source;
  replace(source,"__RWA_GMEM__",name); replace(source,"__RWA_BASE__",std::to_string(base));
  { std::ofstream file(p.root/"Effects"/"bridge",std::ios::binary); file<<source; if (!file) throw Error("IO_ERROR", "Cannot create private JSFX bridge source"); }
  p.helper=p.engine->create(p.root.u8string().c_str(),"bridge",nullptr);
  if (!p.helper) throw Error("UNSUPPORTED", "Cannot instantiate the private JSFX bridge");
  p.upload(std::vector<double>(8192));
}
JsfxChannelEngine::~JsfxChannelEngine()=default;
JsfxChannelEngine::Lease JsfxChannelEngine::reserve() {
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  for (int slot=0;slot<32;++slot) { Lease lease{slot}; if (p.call(1,lease)==1) { lease.token=integer(p.get(2),1099511627775ULL); return lease; } }
  throw Error("RESOURCE_LIMIT", "All declared JSFX channel slots are occupied");
}
bool JsfxChannelEngine::bind(Lease& lease, JsfxBinding& binding, int parameter) {
  const auto declared=binding.parameter(parameter,true);
  const auto name=declared.value("name",std::string());
  if ((name!="ReaWebAPI transport v1" && name!="-ReaWebAPI transport v1") ||
      declared["min"]!=-70368744177664.0 || declared["max"]!=70368744177664.0)
    throw Error("PROTOCOL_MISMATCH", "The declared transport parameter does not match the JSFX SDK");
  const auto value=declared["value"].get<double>();
  if (value>=0) return false;
  const auto epoch=integer(-value);
  const auto header=status(lease);
  if(header.empty())return false;
  if(header[2]>0) {
    const auto response=integer(header[2]);
    if(binding.parameter(parameter)["value"].get<double>()==-double(response)) {lease.epoch=response;lease.response=0;return true;}
    // A stale/copied slider cannot acknowledge another VM's fresh response.
    if(lease.response==response) {if(release(lease))lease=reserve();}
    else lease.response=response;
    return false;
  }
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
  p.put(28,double(epoch));p.call(9,lease);
  return false;
}
bool JsfxChannelEngine::valid(const Lease& lease, JsfxBinding& binding, int parameter) {
  return lease.epoch && binding.parameter(parameter)["value"].get<double>()==-double(lease.epoch);
}
int JsfxChannelEngine::renew(Lease& lease,JsfxBinding& binding,int parameter) {
  const double value=binding.parameter(parameter)["value"].get<double>();
  if(value>=0)return -1;
  const auto epoch=integer(-value);
  std::vector<double> header;
  try {header=status(lease);}catch(const Error& error) {if(error.code=="STALE_SESSION")return -1;throw;}
  if(header.empty())return 0;
  if(integer(header[2])==lease.epoch) {
    if(epoch==lease.epoch)return 0;
    const auto now=std::chrono::steady_clock::now();
    if(lease.response!=epoch) {lease.response=epoch;lease.renewal_started=now;}
    // A reconstructed VM cannot renew its predecessor's slot.
    return now-lease.renewal_started<std::chrono::milliseconds(100)?0:-1;
  }
  if(integer(header[2])!=epoch)return -1;
  lease.epoch=epoch;lease.response=0;return 1;
}
bool JsfxChannelEngine::release(const Lease& lease) {
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  return p.call(6,lease)!=0;
}
int JsfxChannelEngine::cancel(const Lease& lease,uint64_t request,bool allow_running) {
  if(!request || request>9007199254740991ULL)throw Error("INVALID_ARGUMENT", "Invalid JSFX command request");
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);p.put(9,double(request));p.put(23,allow_running?1:0);
  const auto result=p.call(7,lease);
  if(result<0)throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  return result;
}
bool JsfxChannelEngine::subscriptions(const Lease& lease,unsigned mask) {
  if(mask>255)throw Error("INVALID_ARGUMENT", "Invalid JSFX stream subscription mask");
  auto& p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);p.put(27,mask);
  const auto result=p.call(8,lease);
  if(result<0)throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  return result==1;
}
std::vector<double> JsfxChannelEngine::status(const Lease& lease) {
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  const auto status=p.call(2,lease);
  if (status<0) throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  if(!status)return {};
  auto header=p.download(64);
  for(size_t i=0;i<header.size();++i) {
    if(i==20) {if(!std::isfinite(header[i]))throw Error("PROTOCOL_MISMATCH", "Invalid JSFX transport position");}
    else integer(header[i]);
  }
  return header;
}
bool JsfxChannelEngine::receive(const Lease& lease, Packet& packet) {
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  const auto status=p.call(3,lease);
  if (status<0) throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  if (!status) return false;
  const auto count=integer(p.get(65),4096);
  const auto data=p.download(size_t(count)+16);
  packet.id=unsigned(integer(data[0],255)); packet.channels=unsigned(integer(data[2],32));
  packet.sample_rate=unsigned(integer(data[3],768000)); packet.first_sample=integer(data[4]);
  packet.sequence=integer(data[5]); packet.dropped=integer(data[6]); packet.epoch=integer(data[7]);
  packet.position=data[8]; packet.play_state=int(integer(data[9],7));
  packet.clock_epoch=integer(data[10]);
  if (!std::isfinite(packet.position) || std::any_of(data.begin()+16,data.end(),[](double v){return !std::isfinite(v);}))
    throw Error("PROTOCOL_MISMATCH", "Non-finite JSFX telemetry value");
  if (packet.epoch!=lease.epoch || !packet.channels || !packet.sample_rate) throw Error("STALE_SESSION", "JSFX stream generation changed");
  packet.values.assign(data.begin()+16,data.end()); return true;
}
bool JsfxChannelEngine::send(const Lease& lease, uint64_t request, unsigned kind, unsigned id, const std::vector<double>& values,uint64_t owner) {
  if (values.size()>8192 || request>9007199254740991ULL || owner>9007199254740991ULL || !kind || id>255 ||
      std::any_of(values.begin(),values.end(),[](double v){return !std::isfinite(v);})) throw Error("INVALID_ARGUMENT", "Invalid JSFX command payload");
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  p.put(9,double(request)); p.put(10,kind); p.put(11,id); p.put(12,double(values.size()));p.put(15,double(owner)); p.upload(values);
  const auto status=p.call(4,lease);
  if (status<0) throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  return status==1;
}
bool JsfxChannelEngine::reply(const Lease& lease, Reply& reply) {
  auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
  const auto status=p.call(5,lease);
  if (status<0) throw Error("STALE_SESSION", "JSFX buffer ownership changed");
  if (!status) return false;
  reply.request=integer(p.get(9)); reply.code=int(integer(p.get(13),65535));
  reply.values=p.download(size_t(integer(p.get(14),8192)));
  if(std::any_of(reply.values.begin(),reply.values.end(),[](double value){return !std::isfinite(value);}))
    throw Error("PROTOCOL_MISMATCH", "Non-finite JSFX command result");
  return true;
}
}
