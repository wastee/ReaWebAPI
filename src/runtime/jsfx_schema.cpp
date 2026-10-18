#include "runtime/jsfx_schema.hpp"
#include <cmath>
#include <regex>
#include <set>

namespace reaweb {
namespace {
void fields(const Json& object, std::initializer_list<const char*> allowed) {
  if (!object.is_object()) throw Error("INVALID_ARGUMENT", "Expected a JSFX schema object");
  for (auto it=object.begin();it!=object.end();++it) {
    if (std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return key==it.key();}))
      throw Error("INVALID_ARGUMENT", "Unknown JSFX declaration field: "+it.key());
  }
}
unsigned bounded(const Json& value, unsigned minimum, unsigned maximum) {
  if (!value.is_number_integer() || value.get<double>()<minimum || value.get<double>()>maximum)
    throw Error("INVALID_ARGUMENT", "JSFX declaration exceeds its limit");
  return value.get<unsigned>();
}
size_t validate(const Json& s, unsigned depth=0) {
  if (depth>8 || !s.is_object() || !s.value("type",Json()).is_string()) throw Error("INVALID_ARGUMENT", "Invalid JSFX value schema");
  const auto type=s["type"].get<std::string>();
  size_t size=0;
  if (type=="number" || type=="integer") {
    fields(s,{"type","minimum","maximum"});
    if (!s.value("minimum",Json()).is_number() || !s.value("maximum",Json()).is_number())
      throw Error("INVALID_ARGUMENT", "Numeric schemas require finite minimum and maximum");
    const double lo=s["minimum"], hi=s["maximum"];
    if (!std::isfinite(lo)||!std::isfinite(hi)||lo>hi ||
        (type=="integer"&&(lo < -9007199254740991.0 || hi > 9007199254740991.0)))
      throw Error("INVALID_ARGUMENT", "Invalid JSFX schema numeric bounds");
    size=1;
  } else if(type=="boolean") { fields(s,{"type"}); size=1; }
  else if(type=="string") {
    fields(s,{"type","maxBytes"});size=1+bounded(s.value("maxBytes",Json()),0,4096);
  } else if(type=="array") {
    fields(s,{"type","minItems","maxItems","items"});
    const auto maximum=bounded(s.value("maxItems",Json()),0,4096);
    bounded(s.value("minItems",Json(0)),0,maximum);
    size=1+maximum*validate(s.value("items",Json()),depth+1);
  } else if(type=="object") {
    fields(s,{"type","properties"});
    if (!s.value("properties",Json()).is_object() || s["properties"].size()>128)
      throw Error("INVALID_ARGUMENT", "Expected bounded required object properties");
    for (auto it=s["properties"].begin();it!=s["properties"].end();++it) {
      if (!std::regex_match(it.key(),std::regex("[A-Za-z][A-Za-z0-9_.-]{0,127}"))) throw Error("INVALID_ARGUMENT", "Invalid JSFX schema field name");
      size+=validate(it.value(),depth+1);
    }
  } else throw Error("INVALID_ARGUMENT", "Unsupported JSFX schema type");
  if (size>4096) throw Error("RESOURCE_LIMIT", "JSFX state encoding exceeds 4096 scalar values");
  return size;
}
void encode_value(const Json& s,const Json& value,std::vector<double>& out) {
  const auto type=s["type"].get<std::string>();
  if(type=="number"||type=="integer") {
    if (!value.is_number()) throw Error("INVALID_ARGUMENT", "Expected a JSFX numeric value");
    const double v=value;
    if (!std::isfinite(v)||v<s["minimum"].get<double>()||v>s["maximum"].get<double>()||
        (type=="integer"&&v!=std::floor(v)))throw Error("INVALID_ARGUMENT", "JSFX value is outside its schema");
    out.push_back(v);
  } else if(type=="boolean") {
    if(!value.is_boolean())throw Error("INVALID_ARGUMENT", "Expected a JSFX boolean");
    out.push_back(value.get<bool>()?1:0);
  } else if(type=="string") {
    if(!value.is_string())throw Error("INVALID_ARGUMENT", "Expected a JSFX UTF-8 string");
    const auto& text=value.get_ref<const std::string&>();
    if(text.size()>s["maxBytes"].get<size_t>())throw Error("INVALID_ARGUMENT", "JSFX string exceeds its schema");
    // Validate UTF-8 even when called from a native consumer instead of JSON parsing.
    value.dump();out.push_back(double(text.size()));for(unsigned char c:text)out.push_back(c);
  } else if(type=="array") {
    if(!value.is_array()||value.size()<s.value("minItems",size_t(0))||value.size()>s["maxItems"].get<size_t>())
      throw Error("INVALID_ARGUMENT", "JSFX array exceeds its schema");
    out.push_back(double(value.size()));for(const auto& item:value)encode_value(s["items"],item,out);
  } else {
    const auto& properties=s["properties"];
    if(!value.is_object()||value.size()!=properties.size())throw Error("INVALID_ARGUMENT", "JSFX object fields do not match its schema");
    for(auto it=properties.begin();it!=properties.end();++it) {
      if(!value.contains(it.key()))throw Error("INVALID_ARGUMENT", "Missing JSFX object field: "+it.key());
      encode_value(it.value(),value[it.key()],out);
    }
  }
}
Json decode_value(const Json& s,const std::vector<double>& values,size_t& at) {
  const auto next=[&]() {
    if(at>=values.size()||!std::isfinite(values[at]))throw Error("PROTOCOL_MISMATCH", "Truncated JSFX state encoding");
    return values[at++];
  };
  const auto type=s["type"].get<std::string>();
  if(type=="number")return next();
  if(type=="integer") {
    const double v=next();if(std::floor(v)!=v||std::abs(v)>9007199254740991.0)throw Error("PROTOCOL_MISMATCH", "Invalid JSFX integer");
    return int64_t(v);
  }
  if(type=="boolean") {const double v=next();if(v!=0&&v!=1)throw Error("PROTOCOL_MISMATCH", "Invalid JSFX boolean");return v==1;}
  if(type=="string"||type=="array") {
    const double n=next();const auto maximum=s[type=="string"?"maxBytes":"maxItems"].get<size_t>();
    if(n<0||std::floor(n)!=n||n>maximum)throw Error("PROTOCOL_MISMATCH", "Invalid JSFX encoded length");
    if(type=="string") {
      std::string text;for(size_t i=0;i<size_t(n);++i) {
        const double c=next();if(c<0||c>255||std::floor(c)!=c)throw Error("PROTOCOL_MISMATCH", "Invalid JSFX UTF-8 byte");text.push_back(char(c));
      }
      return text;
    }
    Json result=Json::array();for(size_t i=0;i<size_t(n);++i)result.push_back(decode_value(s["items"],values,at));return result;
  }
  Json result=Json::object();for(auto it=s["properties"].begin();it!=s["properties"].end();++it)result[it.key()]=decode_value(it.value(),values,at);return result;
}
}
JsfxSchema::JsfxSchema(Json schema):schema_(std::move(schema)),capacity_(validate(schema_)) {}
std::vector<double> JsfxSchema::encode(const Json& value) const {
  std::vector<double> out;out.reserve(capacity_);encode_value(schema_,value,out);
  if(value.dump().size()>65536)throw Error("RESOURCE_LIMIT", "JSFX JSON state exceeds 64 KiB");
  return out;
}
Json JsfxSchema::decode(const std::vector<double>& values) const {
  if(values.size()>capacity_)throw Error("PROTOCOL_MISMATCH", "JSFX value exceeds its declared capacity");
  size_t at=0;auto value=decode_value(schema_,values,at);
  if(at!=values.size())throw Error("PROTOCOL_MISMATCH", "Trailing JSFX state data");
  try{encode(value);}catch(const std::exception&){throw Error("PROTOCOL_MISMATCH", "JSFX state does not match its schema");}
  return value;
}
JsfxNativeDeclaration::JsfxNativeDeclaration(const Json& input):value(input) {
  fields(input,{"protocolVersion","gmem","base","transportParam","streams","dataStates","commands","resources"});
  if(input.value("protocolVersion",Json())!=1)throw Error("PROTOCOL_MISMATCH", "Unsupported JSFX native protocol");
  if(!input.value("gmem",Json()).is_string())throw Error("INVALID_ARGUMENT", "A declared JSFX gmem name is required");
  gmem=input["gmem"].get<std::string>();
  if(!std::regex_match(gmem,std::regex("[A-Za-z][A-Za-z0-9_.-]{0,127}")))throw Error("INVALID_ARGUMENT", "Invalid JSFX gmem name");
  base=bounded(input.value("base",Json()),0,4194304);parameter=int(bounded(input.value("transportParam",Json()),0,255));
  for(const auto category:{"streams","dataStates","commands","resources"}) {
    if(!value.contains(category))value[category]=Json::array();
    auto& list=value[category];const bool stream=std::string(category)=="streams",resource=std::string(category)=="resources",state=std::string(category)=="dataStates";
    if(!list.is_array()||list.size()>(stream||resource?8:16))throw Error("RESOURCE_LIMIT", "Too many JSFX declarations");
    std::set<std::string> ids;
    for(auto& item:list) {
      if(!item.is_object()||!item.value("id",Json()).is_string())throw Error("INVALID_ARGUMENT", "Expected a JSFX declaration ID");
      const auto id=item["id"].get<std::string>();
      if(!std::regex_match(id,std::regex("[A-Za-z][A-Za-z0-9_.-]{0,127}"))||!ids.insert(id).second)throw Error("INVALID_ARGUMENT", "Invalid or duplicate JSFX declaration ID");
      if(stream) {
        fields(item,{"id","kind","source","channels","channelLabels","fftSize","points","updateRate"});
        if(!item.value("kind",Json()).is_string())throw Error("INVALID_ARGUMENT", "Expected a JSFX stream kind");
        const auto kind=item["kind"].get<std::string>();
        if(kind!="audio"&&kind!="spectrum"&&kind!="waveform"&&kind!="meter"&&kind!="binary"&&kind!="midi")throw Error("INVALID_ARGUMENT", "Invalid JSFX stream kind");
        bounded(item.value("channels",Json()),1,32);
        if(!item.value("source",Json()).is_string()||item["source"].get<std::string>().empty()||item["source"].get<std::string>().size()>128)throw Error("INVALID_ARGUMENT", "JSFX streams require an explicit sampling source");
        const auto fft=bounded(item.value("fftSize",Json(2048)),32,32768);
        if(fft&(fft-1))throw Error("INVALID_ARGUMENT", "FFT size must be a power of two");
        bounded(item.value("points",Json(512)),1,2048);bounded(item.value("updateRate",Json(30)),1,120);
        if(item.contains("channelLabels")) {
          if(!item["channelLabels"].is_array()||item["channelLabels"].size()!=item["channels"].get<size_t>())throw Error("INVALID_ARGUMENT", "Invalid JSFX channel labels");
          for(const auto& label:item["channelLabels"])if(!label.is_string()||label.get<std::string>().size()>128)throw Error("INVALID_ARGUMENT", "Invalid JSFX channel label");
        }
      } else if(resource) {
        fields(item,{"id","format","maxBytes","persistent","undo"});
        if(item.value("format",Json())!="float32"&&item.value("format",Json())!="bytes")throw Error("INVALID_ARGUMENT", "Unsupported JSFX resource format");
        bounded(item.value("maxBytes",Json()),1,32*1024*1024);
      } else if(state) {
        fields(item,{"id","schema","schemaVersion","readOnly","persistent","undo"});
        JsfxSchema schema(item.value("schema",Json()));bounded(item.value("schemaVersion",Json()),1,65535);
      } else {
        fields(item,{"id","args","result","cancellable","persistent","undo","maxExecutionMs","continuous"});
        JsfxSchema args(item.value("args",Json())), result(item.value("result",Json()));
        bounded(item.value("maxExecutionMs",Json(30000)),1,600000);
      }
      for(const auto flag:{"readOnly","persistent","undo","cancellable","continuous"})if(item.contains(flag)&&!item[flag].is_boolean())throw Error("INVALID_ARGUMENT", "Expected a JSFX capability flag");
      if(item.value("continuous",false) && (!item.value("cancellable",false) || item.value("persistent",false) || item.value("undo",false)))
        throw Error("INVALID_ARGUMENT", "Continuous JSFX commands must be cancellable and transient");
    }
  }
}
}
