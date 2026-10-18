#include "runtime/jsfx_schema.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(0)
using namespace reaweb;
template<class F> bool fails(F fn,const char* code) {try{fn();}catch(const Error& e){return e.code==code;}return false;}
int main() {
  try {
    const Json scalar{{"type","number"},{"minimum",-1},{"maximum",1}};
    const Json schema{{"type","object"},{"properties",{
      {"curve",{{"type","array"},{"minItems",2},{"maxItems",4},{"items",scalar}}},
      {"enabled",{{"type","boolean"}}},{"name",{{"type","string"},{"maxBytes",16}}}}}};
    JsfxSchema codec(schema);
    const Json value{{"curve",{0.25,-0.5}},{"enabled",true},{"name","\xe9\x9f\xb3"}};
    auto encoded=codec.encode(value);CHECK(codec.decode(encoded)==value);
    auto invalid=value;invalid["curve"]={0,2};CHECK(fails([&]{codec.encode(invalid);},"INVALID_ARGUMENT"));
    invalid=value;invalid["extra"]=true;CHECK(fails([&]{codec.encode(invalid);},"INVALID_ARGUMENT"));
    encoded.pop_back();CHECK(fails([&]{codec.decode(encoded);},"PROTOCOL_MISMATCH"));
    encoded=codec.encode(value);encoded.push_back(0);CHECK(fails([&]{codec.decode(encoded);},"PROTOCOL_MISMATCH"));
    JsfxSchema string({{"type","string"},{"maxBytes",4}});
    CHECK(fails([&]{string.decode({1,255});},"PROTOCOL_MISMATCH"));
    CHECK(fails([&]{string.decode({4294967296.0});},"PROTOCOL_MISMATCH"));
    CHECK(fails([&]{JsfxSchema({{"type","array"},{"maxItems",4096},{"items",scalar}});},"RESOURCE_LIMIT"));
    JsfxSchema integer({{"type","integer"},{"minimum",-9007199254740991LL},{"maximum",9007199254740991LL}});
    CHECK(integer.decode(integer.encode(9007199254740991LL))==9007199254740991LL);
    CHECK(fails([&]{integer.encode(9007199254740992LL);},"INVALID_ARGUMENT"));
    Json declaration{{"protocolVersion",1},{"gmem","Existing_Effect_Memory"},{"base",65536},{"transportParam",10},
      {"streams",Json::array({{{"id","constructor"},{"kind","spectrum"},{"source","output"},{"channels",2}}})},
      {"dataStates",Json::array({{{"id","constructor"},{"schemaVersion",1},{"schema",scalar}}})}};
    JsfxNativeDeclaration parsed(declaration);CHECK(parsed.parameter==10&&parsed.base==65536&&parsed.value["commands"].empty());
    auto continuous=declaration;
    continuous["commands"]=Json::array({{{"id","note"},{"args",scalar},{"result",scalar},{"continuous",true},{"cancellable",true}}});
    CHECK(JsfxNativeDeclaration(continuous).value["commands"][0]["continuous"]==true);
    continuous["commands"][0]["undo"]=true;CHECK(fails([&]{JsfxNativeDeclaration x(continuous);},"INVALID_ARGUMENT"));
    continuous["commands"][0]["undo"]=false;continuous["commands"][0]["cancellable"]=false;
    CHECK(fails([&]{JsfxNativeDeclaration x(continuous);},"INVALID_ARGUMENT"));
    declaration["streams"].push_back(declaration["streams"][0]);CHECK(fails([&]{JsfxNativeDeclaration x(declaration);},"INVALID_ARGUMENT"));
    declaration["streams"]=Json::array();declaration["gmem"]="bad\noptions:no_meter";
    CHECK(fails([&]{JsfxNativeDeclaration x(declaration);},"INVALID_ARGUMENT"));
    std::cout<<"JSFX bounded schemas, UTF-8, numeric precision and declaration isolation passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
