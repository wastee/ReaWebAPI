#include "runtime/jsfx_control.hpp"
#include "runtime/external_settings.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>

namespace reaweb {
namespace {
using Time=std::chrono::steady_clock;
uint64_t revision(const Json& value) {
  if(!value.is_string())throw Error("INVALID_ARGUMENT", "Expected a decimal revision string");
  const auto text=value.get<std::string>();uint64_t result=0;
  if(text.empty() || (text.size()>1 && text[0]=='0'))throw Error("INVALID_ARGUMENT", "Invalid revision");
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
  if(parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size())throw Error("INVALID_ARGUMENT", "Invalid revision");
  return result;
}
uint64_t limbs(double lo,double hi) {
  if(lo<0 || hi<0 || lo>4294967295.0 || hi>4294967295.0 || lo!=std::floor(lo) || hi!=std::floor(hi))
    throw Error("PROTOCOL_MISMATCH", "Invalid JSFX revision limbs");
  return uint64_t(lo)|(uint64_t(hi)<<32);
}
bool terminal(const Json& value) {return value["status"]=="succeeded" || value["status"]=="failed" || value["status"]=="cancelled";}
std::string string_field(const Json& payload,const char* key) {
  const auto value=payload.value(key,Json());
  if(!value.is_string() || value.get_ref<const std::string&>().empty() || value.get_ref<const std::string&>().size()>128)
    throw Error("INVALID_ARGUMENT", std::string("Invalid JSFX ")+key);
  return value.get<std::string>();
}
size_t byte_count(const Json& value,size_t maximum=32*1024*1024) {
  if(!value.is_number() || !std::isfinite(value.get<double>()) || value<0 || value>maximum || std::floor(value.get<double>())!=value.get<double>())
    throw Error("INVALID_ARGUMENT", "Invalid JSFX resource byte count");
  return value.get<size_t>();
}
}
struct JsfxControl::Impl {
  struct Transfer {
    std::shared_ptr<JsfxTransfer> channel;
    Context context;
    unsigned id=0,width=1;
    bool upload=false,committed=false,verified=false;
    size_t offset=0;
    uint64_t request=0,revision=0;
    Json metadata=Json::object(),error;
  };
  struct Receipt {std::string fingerprint,transfer;Time::time_point created=Time::now();};
  struct Operation {
    Json value;
    std::string fingerprint;
    unsigned kind=0,id=0;
    uint64_t request=0,revision=1;
    uint64_t owner=0;
    std::vector<double> input;
    bool dispatched=false,undo=false,persistent=false;
    unsigned timeout=30000;
    unsigned phase=0;
    size_t offset=0,chunk=0;
    uint64_t expected=0;
    bool staged=false,cancelling=false,timed_out=false;
    std::shared_ptr<Transfer> transfer;
    std::shared_ptr<const JsfxTransfer::Bytes> bytes;
    Json failure;
    Time::time_point started{},finished{};
  };
  std::shared_ptr<JsfxNativeHub::Node> node;
  std::map<std::string,Operation> operations;
  std::map<uint64_t,unsigned> reads;
  std::vector<Json> states;
  std::vector<Json> resources;
  std::map<std::string,std::shared_ptr<Transfer>> transfers;
  std::map<std::string,Receipt> receipts;
  std::map<std::pair<int,uint64_t>,uint64_t> owners;
  std::set<uint64_t> releases;
  std::vector<std::pair<std::string,Json>> events;
  uint64_t serial=0,capability_revision=1;
  bool ready=false;
  Time::time_point next_read{};
  std::string undo_operation;
  UndoEnd end_undo;
  uint64_t conflicts=0,read_errors=0;
  std::string last_read_error;
  explicit Impl(std::shared_ptr<JsfxNativeHub::Node> n):node(std::move(n)) {reset_states();}
  ~Impl() {for(auto& item:transfers)item.second->channel->close();}
  void reset_states() {
    states.clear();reads.clear();resources.clear();
    for(auto& item:transfers)item.second->channel->close();
    transfers.clear();receipts.clear();owners.clear();releases.clear();
    for(const auto& item:node->declaration.value["dataStates"])
      states.push_back({{"id",item["id"]},{"revision","0"},{"schemaVersion",item["schemaVersion"]},{"value",nullptr},{"available",false}});
    for(const auto& item:node->declaration.value["resources"])
      resources.push_back({{"id",item["id"]},{"available",false}});
  }
  const Json& declaration(const char* category,const std::string& id,unsigned* index=nullptr) const {
    const auto& list=node->declaration.value[category];
    auto it=std::find_if(list.begin(),list.end(),[&](const Json& value){return value["id"]==id;});
    if(it==list.end())throw Error("CAPABILITY_UNAVAILABLE", "The JSFX capability is not declared");
    if(index)*index=unsigned(it-list.begin());
    return *it;
  }
  void changed(Operation& op) {
    op.value["revision"]=std::to_string(++op.revision);events.emplace_back("operationChanged",op.value);
  }
  void finish(Operation& op,const std::string& status,Json value) {
    op.value["status"]=status;op.value.erase("blockedReason");op.value.erase("progress");
    if(status=="succeeded")op.value["result"]=std::move(value);
    else op.value["error"]=std::move(value);
    op.finished=Time::now();changed(op);
    if(op.transfer) {
      op.transfer->channel->close();transfers.erase(op.transfer->channel->id);op.bytes.reset();op.transfer.reset();
    }
    if(status=="succeeded" && op.persistent)events.emplace_back("projectChanged",Json::object());
    if(undo_operation==op.value["operationId"]) {
      undo_operation.clear();if(end_undo)end_undo();end_undo={};
    }
  }
  Json state_reply(unsigned id,const std::vector<double>& values,const std::string& source) {
    const auto& decl=node->declaration.value["dataStates"].at(id);
    if(values.size()<4 || values[0]!=decl["schemaVersion"].get<double>() || values[3]!=double(values.size()-4))
      throw Error("PROTOCOL_MISMATCH", "JSFX state metadata does not match its declaration");
    auto value=JsfxSchema(decl["schema"]).decode({values.begin()+4,values.end()});
    const auto rev=std::to_string(limbs(values[1],values[2]));
    const bool notify=!states[id]["available"].get<bool>() || states[id]["revision"]!=rev;
    states[id]={{"id",decl["id"]},{"revision",rev},{"schemaVersion",decl["schemaVersion"]},{"value",value},{"available",true}};
    if(notify)events.emplace_back("dataStateChanged",Json{{"id",decl["id"]},{"revision",rev},{"source",source}});
    return {{"id",decl["id"]},{"revision",rev},{"value",std::move(value)}};
  }
  Json resource_reply(unsigned id,const std::vector<double>& values) {
    const auto& decl=node->declaration.value["resources"].at(id);
    const size_t width=decl["format"]=="float32"?4:1;
    if(values.size()<6 || values[3]!=(width==4?2:1) || values[2]<0 || std::floor(values[2])!=values[2] ||
        values[2]*width>decl["maxBytes"].get<size_t>() || values[4]<values[2] || values[4]>33554432 ||
        std::floor(values[4])!=values[4] || values[5]>768 || values[5]!=double(values.size()-6))
      throw Error("PROTOCOL_MISMATCH", "JSFX resource metadata does not match its declaration");
    std::string encoded;
    for(size_t i=6;i<values.size();++i) {
      if(values[i]<0 || values[i]>255 || std::floor(values[i])!=values[i])throw Error("PROTOCOL_MISMATCH", "Invalid JSFX resource metadata byte");
      encoded.push_back(char(values[i]));
    }
    Json metadata=Json::object();
    try {if(!encoded.empty())metadata=Json::parse(encoded);}catch(...) {throw Error("PROTOCOL_MISMATCH", "Invalid JSFX resource metadata JSON");}
    if(!metadata.is_object())throw Error("PROTOCOL_MISMATCH", "JSFX resource metadata must be an object");
    resources[id]={{"id",decl["id"]},{"revision",std::to_string(limbs(values[0],values[1]))},
      {"totalBytes",size_t(values[2])*width},{"format",decl["format"]},{"metadata",metadata},{"available",true},{"capacity",size_t(values[4])*width}};
    return resources[id];
  }
  std::shared_ptr<Transfer> transfer(const std::string& id,const Context& context) {
    auto found=transfers.find(id);
    if(found==transfers.end() || found->second->channel->closed())throw Error("TRANSFER_NOT_FOUND", "JSFX resource transfer not found");
    auto result=found->second;
    if(result->context.window!=context.window || result->context.document!=context.document || result->context.session!=context.session)
      throw Error("TRANSFER_NOT_FOUND", "JSFX resource transfer belongs to another document");
    return result;
  }
  void transfer_limit() const {
    if(transfers.size()>=2)throw Error("RESOURCE_LIMIT", "JSFX instance transfer limit reached");
  }
  void resource_input(Operation& op) {
    op.input.clear();
    if(op.phase==4) {
      const auto metadata=op.transfer->metadata.dump();
      op.input={double(op.bytes->size()/op.transfer->width),double(metadata.size())};
      for(unsigned char c:metadata)op.input.push_back(c);
    } else if(op.phase==5) {
      const size_t width=op.transfer->width;op.chunk=std::min(size_t(4096),(op.bytes->size()-op.offset)/width);
      op.input={double(op.offset/width),double(op.chunk)};
      for(size_t i=0;i<op.chunk;++i) {
        const auto at=op.offset+i*width;
        if(width==1)op.input.push_back((*op.bytes)[at]);
        else {
          const uint32_t bits=uint32_t((*op.bytes)[at])|(uint32_t((*op.bytes)[at+1])<<8)|(uint32_t((*op.bytes)[at+2])<<16)|(uint32_t((*op.bytes)[at+3])<<24);
          float value;std::memcpy(&value,&bits,4);
          if(!std::isfinite(value))throw Error("INVALID_ARGUMENT", "JSFX float32 resources require finite values");
          op.input.push_back(value);
        }
      }
    } else if(op.phase==6)op.input={double(op.expected&0xffffffffULL),double(op.expected>>32)};
  }
  void download_reply(Transfer& transfer,const JsfxNativeHub::Result& reply) {
    transfer.request=0;
    if(reply.code)throw Error(reply.code==1?"CONFLICT":"PROTOCOL_MISMATCH", "JSFX resource changed while taking its snapshot");
    const auto& values=reply.values;
    if(values.size()<6 || limbs(values[0],values[1])!=transfer.revision ||
        values[2]*transfer.width!=transfer.channel->total() || values[3]!=(transfer.width==4?2:1) ||
        values[4]*transfer.width!=transfer.offset || values[5]!=double(values.size()-6))
      throw Error("PROTOCOL_MISMATCH", "Invalid JSFX resource snapshot range");
    JsfxTransfer::Bytes bytes;bytes.reserve((values.size()-6)*transfer.width);
    for(size_t i=6;i<values.size();++i) {
      if(transfer.width==1) {
        if(values[i]<0 || values[i]>255 || std::floor(values[i])!=values[i])throw Error("PROTOCOL_MISMATCH", "Invalid JSFX resource byte");
        bytes.push_back(static_cast<unsigned char>(values[i]));
      } else {
        const float value=static_cast<float>(values[i]);
        if(!std::isfinite(value))throw Error("PROTOCOL_MISMATCH", "Invalid JSFX float32 resource value");
        uint32_t bits;std::memcpy(&bits,&value,4);
        for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<unsigned char>(bits>>(n*8)));
      }
    }
    transfer.channel->append_snapshot(bytes);transfer.offset+=bytes.size();
    transfer.verified=true;
  }
  void reap() {
    const auto now=Time::now();
    for(auto it=operations.begin();it!=operations.end();)
      if(terminal(it->second.value) && now-it->second.finished>=std::chrono::seconds(60))it=operations.erase(it);else ++it;
    for(auto it=transfers.begin();it!=transfers.end();)
      if(!it->second->committed && it->second->channel->expired()) {it->second->channel->close();it=transfers.erase(it);}else ++it;
    for(auto it=receipts.begin();it!=receipts.end();)
      if(now-it->second.created>=std::chrono::seconds(60))it=receipts.erase(it);else ++it;
  }
  Operation& operation(const std::string& id) {
    auto it=operations.find(id);
    if(it==operations.end())throw Error("OPERATION_NOT_FOUND", "The JSFX operation does not exist or has expired");
    return it->second;
  }
  bool release_owner(int window,uint64_t document=0) {
    bool released=false;
    for(auto it=owners.begin();it!=owners.end();) {
      if(it->first.first!=window || (document && it->first.second!=document)) {++it;continue;}
      const auto owner=it->second;
      for(auto& entry:operations) {
        auto& op=entry.second;
        if(op.owner!=owner || terminal(op.value))continue;
        int result=0;
        try {result=op.dispatched?node->cancel(op.request,true):1;}
        catch(const Error&) {} // A lost DSP lease is cleaned up by its next generation.
        if(result==1)finish(op,"cancelled",{{"code","CANCELLED"},{"message","The continuous command owner was released"}});
      }
      releases.insert(owner);it=owners.erase(it);released=true;
    }
    return released;
  }
};
JsfxControl::JsfxControl(std::shared_ptr<JsfxNativeHub::Node> node):impl_(new Impl(std::move(node))){}
JsfxControl::~JsfxControl() {if(impl_->end_undo)impl_->end_undo();}
int JsfxControl::parameter() const {return impl_->node->declaration.parameter;}
bool JsfxControl::idle() const {return impl_->operations.empty() && impl_->transfers.empty() && impl_->receipts.empty() && impl_->releases.empty();}
bool JsfxControl::tick(JsfxBinding& binding,const UndoBegin& begin,const UndoEnd& end) {
  auto& p=*impl_;p.reap();
  if(p.node->refresh(binding)) {
    if(p.end_undo)p.end_undo();
    p.end_undo={};p.undo_operation.clear();
    p.operations.clear();p.events.clear();p.reset_states();++p.capability_revision;p.next_read={};return true;
  }
  const bool ready=p.node->available();
  if(ready!=p.ready) {
    p.ready=ready;++p.capability_revision;
    p.events.emplace_back("capabilitiesChanged",Json{{"capabilityRevision",std::to_string(p.capability_revision)},{"reason",ready?"resumed":"DSP_UNAVAILABLE"}});
  }
  for(auto& reply:p.node->results()) {
    auto read=p.reads.find(reply.request);
    if(read!=p.reads.end()) {
      const auto index=read->second;p.reads.erase(read);
      try {
        if(reply.code)throw Error("PROTOCOL_MISMATCH", "JSFX rejected a declared state or resource read");
        if(index<16)p.state_reply(index,reply.values,"dsp");else p.resource_reply(index-16,reply.values);
      } catch(const Error& error) {
        (index<16?p.states[index]:p.resources[index-16])["available"]=false;
        ++p.read_errors;p.last_read_error=error.code;
      }
      continue;
    }
    auto download=std::find_if(p.transfers.begin(),p.transfers.end(),[&](const auto& item){return !item.second->upload && item.second->request==reply.request;});
    if(download!=p.transfers.end()) {
      try {p.download_reply(*download->second,reply);}
      catch(const Error& error) {download->second->error={{"code",error.code},{"message",error.what()}};download->second->request=0;}
      continue;
    }
    auto found=std::find_if(p.operations.begin(),p.operations.end(),[&](auto& op){return op.second.request==reply.request;});
    if(found==p.operations.end() || terminal(found->second.value))continue;
    auto& op=found->second;
    try {
      if(op.kind==6 && op.phase==8) {
        p.finish(op,op.cancelling?"cancelled":"failed",op.failure);continue;
      }
      if(op.kind==6 && !reply.code && op.phase<6) {
        if(op.phase==4)op.staged=true;
        else op.offset+=op.chunk*op.transfer->width;
        op.phase=op.offset==op.bytes->size()?6:5;
        op.dispatched=false;op.request=++p.serial;
        op.value["progress"]=op.bytes->empty()?1.0:double(op.offset)/op.bytes->size();p.changed(op);continue;
      }
      if(!reply.code) {
        auto result=op.kind==3?p.state_reply(op.id,reply.values,"web"):op.kind==6?p.resource_reply(op.id,reply.values):
          JsfxSchema(p.node->declaration.value["commands"][op.id]["result"]).decode(reply.values);
        if(op.kind==6) {result.erase("available");result.erase("capacity");}
        if(op.undo || op.persistent) {
          // A gmem commit does not invalidate REAPER's cached FX undo state.
          // Reapply only the reserved bootstrap value before closing the undo block.
          const int parameter=p.node->declaration.parameter;
          binding.set(parameter,binding.parameter(parameter)["value"].get<double>(),false);
        }
        p.finish(op,"succeeded",result);
      } else {
        Json details=Json::object();
        if(reply.code==1 && op.kind==3) {details=p.state_reply(op.id,reply.values,"dsp");++p.conflicts;}
        if(reply.code==1 && op.kind==6) {details=p.resource_reply(op.id,reply.values);++p.conflicts;}
        const char* code=reply.code==1?"CONFLICT":reply.code==3?"INVALID_ARGUMENT":reply.code==6?"CANCELLED":
          reply.code==2?"INVALID_ARGUMENT":reply.code==5?"QUEUE_LIMIT":"PROTOCOL_MISMATCH";
        op.failure={{"code",code},{"message","JSFX rejected the operation"},{"details",details}};
        if(op.kind==6 && op.staged) {op.phase=8;op.request=++p.serial;op.dispatched=false;}
        else p.finish(op,reply.code==6?"cancelled":"failed",op.failure);
      }
    }catch(const Error& error) {
      op.failure={{"code",error.code},{"message",error.what()}};
      if(op.kind==6 && op.staged && op.phase<6) {op.phase=8;op.request=++p.serial;op.dispatched=false;}
      else p.finish(op,"failed",op.failure);
    }
  }
  const auto now=Time::now();
  for(auto it=p.releases.begin();it!=p.releases.end();) {
    try {p.node->enqueue(++p.serial,9,0,{double(*it)});it=p.releases.erase(it);}
    catch(const Error& error) {if(error.code=="QUEUE_LIMIT")break;throw;}
  }
  std::vector<Impl::Operation*> pending;
  for(auto& entry:p.operations)if(!terminal(entry.second.value))pending.push_back(&entry.second);
  std::sort(pending.begin(),pending.end(),[](const auto* a,const auto* b){return a->request<b->request;});
  for(auto* operation:pending) {
    auto& op=*operation;
    if(op.dispatched) {
      if(p.node->running(op.request) && op.value["status"]!="running") {
        op.value["status"]="running";op.value.erase("blockedReason");op.started=now;p.changed(op);
      }
      if(op.kind==1 && !op.timed_out && op.value["status"]=="running" && now-op.started>std::chrono::milliseconds(op.timeout)) {
        p.node->cancel(op.request,p.node->declaration.value["commands"][op.id].value("cancellable",false));
        // Keep the execution and Undo record alive until the DSP acknowledges.
        op.timed_out=true;op.value["deadlineExceeded"]=true;p.changed(op);
      }
      if(!ready && op.value.value("blockedReason",std::string())!="DSP_UNAVAILABLE") {
        op.value["blockedReason"]="DSP_UNAVAILABLE";p.changed(op);
      } else if(ready && op.value.contains("blockedReason")) {op.value.erase("blockedReason");p.changed(op);}
      continue;
    }
    if(!ready) {
      if(op.value.value("blockedReason",std::string())!="DSP_UNAVAILABLE") {op.value["blockedReason"]="DSP_UNAVAILABLE";p.changed(op);}
      continue;
    }
    if(op.kind==6 && op.cancelling)op.phase=8;
    if(op.undo && (op.kind!=6 || op.phase==6) && p.undo_operation!=op.value["operationId"]) {
      if(!p.undo_operation.empty() || !begin("JSFX Web UI state"))continue;
      p.undo_operation=op.value["operationId"].get<std::string>();p.end_undo=end;
    }
    try {
      if(op.kind==6)p.resource_input(op);
      p.node->enqueue(op.request,op.kind==6?op.phase:op.kind,op.id,op.input,op.owner);op.dispatched=true;op.value.erase("blockedReason");p.changed(op);
    }
    catch(const Error& error) {
      if(error.code=="QUEUE_LIMIT")continue;
      if(op.kind==6 && op.staged) {
        op.phase=8;op.failure={{"code",error.code},{"message",error.what()}};op.request=++p.serial;continue;
      }
      p.finish(op,"failed",{{"code",error.code},{"message",error.what()}});
    }
    catch(...) {
      if(p.undo_operation==op.value["operationId"]) {p.undo_operation.clear();if(p.end_undo)p.end_undo();p.end_undo={};}
      throw;
    }
  }
  if(ready)for(auto& entry:p.transfers) {
    auto& transfer=*entry.second;
    if(transfer.upload || transfer.request || !transfer.error.is_null() || (transfer.verified && transfer.channel->complete()))continue;
    const auto request=++p.serial;
    const size_t count=std::min(size_t(4096),(transfer.channel->total()-transfer.offset)/transfer.width);
    try {p.node->enqueue(request,7,transfer.id,{double(transfer.revision&0xffffffffULL),double(transfer.revision>>32),double(transfer.offset/transfer.width),double(count)});transfer.request=request;}
    catch(const Error& error) {if(error.code!="QUEUE_LIMIT")transfer.error={{"code",error.code},{"message",error.what()}};}
  }
  if(ready && now>=p.next_read) {
    for(unsigned offset=0;offset<p.states.size()+p.resources.size();++offset) {
      const bool resource=offset>=p.states.size();const unsigned id=resource?offset-unsigned(p.states.size()):offset,key=resource?id+16:id;
      if(std::any_of(p.reads.begin(),p.reads.end(),[&](const auto& read){return read.second==key;}))continue;
      const auto request=++p.serial;
      try {p.node->enqueue(request,resource?10:2,id,{});p.reads.emplace(request,key);}catch(const Error&){break;}
    }
    p.next_read=now+std::chrono::milliseconds(250);
  }
  return false;
}
Json JsfxControl::capabilities(const std::string& session) const {
  const auto& p=*impl_;const auto& value=p.node->declaration.value;
  return {{"protocolVersion",1},{"sessionId",session},{"capabilityRevision",std::to_string(p.capability_revision)},
    {"status",p.ready?"ready":"suspended"},{"streams",value["streams"]},{"dataStates",value["dataStates"]},
    {"commands",value["commands"]},{"resources",value["resources"]},
    {"features",{{"params",true},{"paramGroups",true},{"nativeChannel",true}}},
    {"limits",{{"maxParametersPerGesture",259},{"pendingOperations",16},{"operationRecords",256},{"operationRetentionMs",60000},
      {"stateBytes",65536},{"streams",8},{"maxUpdateRate",120},{"resourceTransfers",2},
      {"resourceBytes",32*1024*1024},{"resourceChunkBytes",JsfxTransfer::chunk_bytes},{"resourceMetadataBytes",768}}}};
}
Json JsfxControl::call(const std::string& method,const Json& payload,const Context& context) {
  auto& p=*impl_;p.reap();
  if(method=="releaseCommands")return {{"released",p.release_owner(context.window,context.document)}};
  if(method=="openStream")return p.node->open_stream(string_field(payload,"id"),payload.value("options",Json::object()),context.window,context.document,context.session);
  if(method=="getDataState") {
    unsigned index=0;p.declaration("dataStates",string_field(payload,"id"),&index);
    auto result=p.states[index];result["available"]=result["available"].get<bool>() && p.ready;return result;
  }
  if(method=="getOperation")return p.operation(string_field(payload,"operationId")).value;
  if(method=="findOperation") {
    const auto request=string_field(payload,"requestId");
    for(const auto& entry:p.operations)if(entry.second.value["requestId"]==request)return {{"operation",entry.second.value}};
    return {{"operation",nullptr}};
  }
  if(method=="cancelOperation") {
    auto& op=p.operation(string_field(payload,"operationId"));
    if(terminal(op.value))return {{"accepted",false},{"operation",op.value}};
    if(op.kind==6) {
      if(op.phase==6 && op.dispatched)return {{"accepted",false},{"operation",op.value}};
      op.cancelling=true;op.failure={{"code","CANCELLED"},{"message","The JSFX resource upload was cancelled"}};
      if(!op.dispatched && !op.staged)p.finish(op,"cancelled",op.failure);
      return {{"accepted",true},{"operation",op.value}};
    }
    const bool allow_running=op.kind==1 && p.node->declaration.value["commands"][op.id].value("cancellable",false);
    const auto result=op.dispatched?p.node->cancel(op.request,allow_running):1;
    if(result==1)p.finish(op,"cancelled",{{"code","CANCELLED"},{"message","The JSFX operation was cancelled before execution"}});
    return {{"accepted",result!=0},{"operation",op.value}};
  }
  if(method=="abortResourceUpload" || method=="closeResourceDownload") {
    const char* key=method=="abortResourceUpload"?"aborted":"closed";
    std::shared_ptr<Impl::Transfer> transfer;
    try {transfer=p.transfer(string_field(payload,"transferId"),context);}
    catch(const Error& error) {if(error.code=="TRANSFER_NOT_FOUND")return {{key,false}};throw;}
    if(transfer->committed || transfer->upload!=(method=="abortResourceUpload"))return {{key,false}};
    transfer->channel->close();p.transfers.erase(transfer->channel->id);return {{key,true}};
  }
  if(method=="openResourceDownload") {
    std::shared_ptr<Impl::Transfer> transfer;
    if(payload.contains("transferId"))transfer=p.transfer(string_field(payload,"transferId"),context);
    else {
      unsigned index=0;p.declaration("resources",string_field(payload,"id"),&index);
      const auto& info=p.resources[index];
      if(!p.ready || !info["available"].get<bool>())throw Error("DSP_UNAVAILABLE", "The JSFX resource is not currently available");
      const auto expected=revision(payload.value("revision",info["revision"]));
      if(std::to_string(expected)!=info["revision"])throw Error("CONFLICT", "The JSFX resource revision changed",info);
      p.transfer_limit();transfer=std::make_shared<Impl::Transfer>();transfer->id=index;transfer->context=context;
      transfer->revision=expected;transfer->metadata=info["metadata"];transfer->width=info["format"]=="float32"?4:1;
      transfer->channel=p.node->create_transfer(info["totalBytes"].get<size_t>(),false);
      p.transfers.emplace(transfer->channel->id,transfer);
    }
    if(transfer->upload)throw Error("INVALID_ARGUMENT", "Expected a JSFX resource download");
    if(!transfer->error.is_null()) {
      const auto error=transfer->error;transfer->channel->close();p.transfers.erase(transfer->channel->id);
      throw Error(error["code"].get<std::string>(),error["message"].get<std::string>());
    }
    if(!transfer->verified || !transfer->channel->complete())return {{"pending",true},{"transferId",transfer->channel->id}};
    return {{"transferId",transfer->channel->id},{"revision",std::to_string(transfer->revision)},
      {"totalBytes",transfer->channel->total()},{"format",transfer->width==4?"float32":"bytes"},
      {"metadata",transfer->metadata},{"maxChunkBytes",JsfxTransfer::chunk_bytes},{"checksum",transfer->channel->checksum()},
      {"transport",p.node->attach_transfer(transfer->channel,context.window,context.document,context.origin)}};
  }
  if(method!="setDataState" && method!="invokeCommand" && method!="beginResourceUpload" && method!="commitResourceUpload")
    throw Error("UNKNOWN_METHOD", "Unknown JSFX native method");
  const auto request=string_field(payload,"requestId");
  Json identity=payload;identity.erase("sessionId");identity.erase("activation");
  const auto fingerprint=Json{{"method",method},{"payload",identity}}.dump();
  for(const auto& entry:p.operations)if(entry.second.value["requestId"]==request) {
    if(entry.second.fingerprint!=fingerprint)throw Error("INVALID_ARGUMENT", "The requestId was already used for another JSFX operation");
    return entry.second.value;
  }
  auto receipt=p.receipts.find(request);
  if(receipt!=p.receipts.end()) {
    if(receipt->second.fingerprint!=fingerprint)throw Error("INVALID_ARGUMENT", "The requestId was already used for another JSFX operation");
    auto transfer=p.transfer(receipt->second.transfer,context);
    return {{"transferId",transfer->channel->id},{"maxChunkBytes",JsfxTransfer::chunk_bytes},
      {"transport",p.node->attach_transfer(transfer->channel,context.window,context.document,context.origin)}};
  }
  if(p.operations.size()+p.receipts.size()>=256 || std::count_if(p.operations.begin(),p.operations.end(),[](const auto& item){return !terminal(item.second.value);})>=16)
    throw Error("QUEUE_LIMIT", "JSFX operation retention or pending limit reached");
  if(method=="beginResourceUpload") {
    unsigned index=0;const auto& decl=p.declaration("resources",string_field(payload,"id"),&index);
    const auto& info=p.resources[index];
    if(!p.ready || !info["available"].get<bool>())throw Error("DSP_UNAVAILABLE", "The JSFX resource is not currently available");
    if(payload.value("format",Json())!=decl["format"])throw Error("INVALID_ARGUMENT", "JSFX resource format does not match its declaration");
    const auto total=byte_count(payload.value("totalBytes",Json()),std::min(decl["maxBytes"].get<size_t>(),info["capacity"].get<size_t>()));
    const auto metadata=payload.value("metadata",Json::object());
    if(!metadata.is_object() || metadata.dump().size()>768)throw Error("INVALID_ARGUMENT", "JSFX resource metadata exceeds 768 UTF-8 bytes");
    const unsigned width=decl["format"]=="float32"?4:1;
    if(total%width)throw Error("INVALID_ARGUMENT", "JSFX float32 resource size must be a multiple of four");
    p.transfer_limit();auto transfer=std::make_shared<Impl::Transfer>();
    transfer->context=context;transfer->id=index;transfer->width=width;transfer->upload=true;transfer->metadata=metadata;
    transfer->channel=p.node->create_transfer(total,true);
    auto transport=p.node->attach_transfer(transfer->channel,context.window,context.document,context.origin);
    p.transfers.emplace(transfer->channel->id,transfer);p.receipts.emplace(request,Impl::Receipt{fingerprint,transfer->channel->id});
    return {{"transferId",transfer->channel->id},{"maxChunkBytes",JsfxTransfer::chunk_bytes},{"transport",transport}};
  }
  if(method=="commitResourceUpload") {
    auto transfer=p.transfer(string_field(payload,"transferId"),context);
    if(!transfer->upload || transfer->committed)throw Error("INVALID_ARGUMENT", "JSFX upload was already committed");
    const auto expected=revision(payload.value("expectedRevision",Json()));
    const auto checksum=string_field(payload,"checksum");
    Impl::Operation op;op.kind=6;op.phase=4;op.id=transfer->id;op.transfer=transfer;
    op.bytes=transfer->channel->seal(checksum);op.expected=expected;
    const auto& decl=p.node->declaration.value["resources"][op.id];op.undo=decl.value("undo",false);op.persistent=decl.value("persistent",false);
    op.fingerprint=fingerprint;op.request=++p.serial;
    const auto id=external_token();op.value={{"operationId",id},{"requestId",request},{"revision","1"},{"status","queued"},{"progress",0}};
    const auto result=op.value;p.operations.emplace(id,std::move(op));transfer->committed=true;return result;
  }
  const bool state=method=="setDataState";unsigned index=0;
  const auto& decl=p.declaration(state?"dataStates":"commands",string_field(payload,"id"),&index);
  if(state && decl.value("readOnly",false))throw Error("INVALID_ARGUMENT", "The JSFX state is read-only");
  auto input=JsfxSchema(decl[state?"schema":"args"]).encode(payload.value(state?"value":"args",Json()));
  if(state) {
    const auto expected=revision(payload.value("expectedRevision",Json()));
    input.insert(input.begin(),{decl["schemaVersion"].get<double>(),double(expected&0xffffffffULL),double(expected>>32),double(input.size())});
  }
  Impl::Operation op;op.kind=state?3:1;op.id=index;op.input=std::move(input);op.request=++p.serial;
  op.fingerprint=fingerprint;op.undo=decl.value("undo",false);op.persistent=decl.value("persistent",false);op.timeout=decl.value("maxExecutionMs",30000u);
  if(decl.value("continuous",false)) {
    const auto key=std::make_pair(context.window,context.document);
    auto owner=p.owners.find(key);
    if(owner==p.owners.end()) {
      if(p.owners.size()+p.releases.size()>=16)throw Error("QUEUE_LIMIT", "Too many JSFX continuous command owners");
      owner=p.owners.emplace(key,++p.serial).first;
    }
    op.owner=owner->second;
  }
  const auto id=external_token();op.value={{"operationId",id},{"requestId",request},{"revision","1"},{"status","queued"}};
  if(!p.ready)op.value["blockedReason"]="DSP_UNAVAILABLE";
  const auto result=op.value;p.operations.emplace(id,std::move(op));return result;
}
Json JsfxControl::diagnostics() const {
  auto result=impl_->node->diagnostics();result["operationRecords"]=impl_->operations.size();result["conflicts"]=std::to_string(impl_->conflicts);
  result["readErrors"]=std::to_string(impl_->read_errors);result["lastReadError"]=impl_->last_read_error;return result;
}
void JsfxControl::close_window(int window) {
  auto& p=*impl_;p.node->close_window(window);p.release_owner(window);
  for(auto it=p.transfers.begin();it!=p.transfers.end();) {
    auto& transfer=*it->second;
    if(transfer.context.window!=window) {++it;continue;}
    transfer.channel->close();
    if(!transfer.committed)it=p.transfers.erase(it);else ++it;
  }
}
std::vector<std::pair<std::string,Json>> JsfxControl::events() {std::vector<std::pair<std::string,Json>> result;result.swap(impl_->events);return result;}
}
