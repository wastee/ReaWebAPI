#include "runtime/jsfx_transfer.hpp"
#include <iostream>
#include <stdexcept>
using namespace reaweb;
#define CHECK(v) do {if(!(v))throw std::runtime_error(#v);}while(0)
using Bytes=JsfxTransfer::Bytes;
void put(Bytes& bytes,size_t at,uint64_t value,unsigned n) {for(unsigned i=0;i<n;++i)bytes[at+i]=static_cast<unsigned char>(value>>(i*8));}
Bytes packet(const std::string& id,bool upload,uint64_t sequence,size_t offset,size_t length,const Bytes& payload={}) {
  Bytes result(104);result[0]='R';result[1]='W';result[2]='R';result[3]=1;result[4]=upload?1:2;result[5]=1;
  put(result,8,sequence,8);put(result,16,offset,8);put(result,24,length,4);std::copy(id.begin(),id.end(),result.begin()+32);
  result.insert(result.end(),payload.begin(),payload.end());put(result,96,JsfxTransfer::crc32(payload.data(),payload.size()),4);return result;
}
int main() {try {
  CHECK(JsfxTransfer::sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(JsfxTransfer::sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(JsfxTransfer::sha256(Bytes(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  const Bytes text{'1','2','3','4','5','6','7','8','9'};
  CHECK(JsfxTransfer::crc32(text.data(),text.size())==0xcbf43926u);
  const auto budget=std::make_shared<JsfxTransfer::Budget>();const std::string id(64,'a');
  {
    JsfxTransfer transfer(budget,id,6,true);
    CHECK(transfer.exchange(packet(id,true,1,2,3,{1,2,3}))[5]==3);
    auto first=packet(id,true,1,0,3,{0,255,128});CHECK(transfer.exchange(first)[5]==2);CHECK(transfer.exchange(first)[5]==2);
    CHECK(transfer.exchange(packet(id,true,1,0,3,{0,255,127}))[5]==3);
    bool incomplete=false;try{transfer.seal("bad");}catch(const Error&){incomplete=true;}CHECK(incomplete);
    CHECK(transfer.exchange(packet(id,true,2,3,3,{0,1,2}))[5]==2);
    const Bytes expected{0,255,128,0,1,2};CHECK(*transfer.seal(JsfxTransfer::sha256(expected))==expected);
    CHECK(transfer.exchange(first)[5]==3);
    transfer.close();CHECK(transfer.closed());
  }
  CHECK(budget->bytes==0);
  {
    auto child=std::make_shared<JsfxTransfer::Budget>();child->parent=budget;child->limit=3*JsfxTransfer::chunk_bytes+3;
    JsfxTransfer transfer(child,id,3,false);
    CHECK(child->bytes==budget->bytes && !transfer.complete());
    CHECK(transfer.exchange(packet(id,false,1,0,3))[5]==3);
    transfer.append_snapshot({'a'});transfer.append_snapshot({'b','c'});
    CHECK(transfer.complete() && transfer.checksum()==JsfxTransfer::sha256({'a','b','c'}));
    bool limited=false;try{JsfxTransfer second(child,id,0,true);}catch(const Error& e){limited=e.code=="RESOURCE_LIMIT";}CHECK(limited);
    CHECK(child->bytes==budget->bytes);
    const auto response=transfer.exchange(packet(id,false,1,0,3));
    CHECK(response[5]==2 && Bytes(response.begin()+104,response.end())==Bytes({'a','b','c'}));
  }
  CHECK(budget->bytes==0);
  {
    const auto snapshot=std::make_shared<const Bytes>(text);JsfxTransfer transfer(budget,id,text.size(),false,snapshot);
    const auto request=packet(id,false,1,1,3);const auto response=transfer.exchange(request);
    CHECK(response[5]==2 && Bytes(response.begin()+104,response.end())==Bytes({'2','3','4'}));
    CHECK(transfer.exchange(request)==response);
    CHECK(transfer.exchange(packet(id,false,2,8,2))[5]==3);
    CHECK(transfer.exchange(packet(id,false,2,0,0))[5]==2);
  }
  CHECK(budget->bytes==0);
  std::cout<<"JSFX SHA-256, CRC32, resource boundaries, retry identity and budget release passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
