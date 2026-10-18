#include "runtime/jsfx_transfer.hpp"
#include <array>
#include <algorithm>
#include <cstring>

namespace reaweb {
namespace {
constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
  0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
  0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,
  0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,
  0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
uint32_t rotate(uint32_t value,unsigned n){return (value>>n)|(value<<(32-n));}
uint64_t read(const JsfxTransfer::Bytes& bytes,size_t at,unsigned size) {
  uint64_t value=0;for(unsigned i=0;i<size;++i)value|=uint64_t(bytes[at+i])<<(i*8);return value;
}
void write(JsfxTransfer::Bytes& bytes,size_t at,uint64_t value,unsigned size) {
  for(unsigned i=0;i<size;++i)bytes[at+i]=static_cast<unsigned char>(value>>(i*8));
}
void reserve(const std::shared_ptr<JsfxTransfer::Budget>& budget,size_t size) {
  auto before=budget->bytes.load();
  do {if(size>budget->limit || before>budget->limit-size)throw Error("RESOURCE_LIMIT", "JSFX resource budget exceeded");}
  while(!budget->bytes.compare_exchange_weak(before,before+size));
  try {if(budget->parent)reserve(budget->parent,size);}catch(...) {budget->bytes.fetch_sub(size);throw;}
}
void release(const std::shared_ptr<JsfxTransfer::Budget>& budget,size_t size) {
  budget->bytes.fetch_sub(size);if(budget->parent)release(budget->parent,size);
}
}
struct JsfxTransfer::Hash {
  std::array<uint32_t,8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<unsigned char,64> pending{};
  uint64_t count=0;size_t used=0;
  void block(const unsigned char* p) {
    uint32_t w[64];
    for(unsigned i=0;i<16;++i)w[i]=(uint32_t(p[i*4])<<24)|(uint32_t(p[i*4+1])<<16)|(uint32_t(p[i*4+2])<<8)|p[i*4+3];
    for(unsigned i=16;i<64;++i)w[i]=w[i-16]+(rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10));
    auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for(unsigned i=0;i<64;++i) {
      const auto x=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+k[i]+w[i];
      const auto y=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
      h=g;g=f;f=e;e=d+x;d=c;c=b;b=a;a=x+y;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
  }
  void add(const unsigned char* p,size_t n) {
    count+=n;
    while(n) {
      const size_t take=std::min(n,64-used);std::memcpy(pending.data()+used,p,take);used+=take;p+=take;n-=take;
      if(used==64){block(pending.data());used=0;}
    }
  }
  std::string digest() const {
    Hash copy=*this;const auto bits=count*8;unsigned char one=128;copy.add(&one,1);
    unsigned char zero=0;while(copy.used!=56)copy.add(&zero,1);
    unsigned char length[8];for(unsigned i=0;i<8;++i)length[7-i]=static_cast<unsigned char>(bits>>(i*8));copy.add(length,8);
    static const char hex[]="0123456789abcdef";std::string result;
    for(auto word:copy.state)for(int i=7;i>=0;--i)result.push_back(hex[(word>>(i*4))&15]);
    return result;
  }
};
std::string JsfxTransfer::sha256(const Bytes& bytes) {Hash hash;hash.add(bytes.data(),bytes.size());return hash.digest();}
uint32_t JsfxTransfer::crc32(const unsigned char* bytes,size_t size) {
  static const auto table=[] {
    std::array<uint32_t,256> result{};
    for(unsigned i=0;i<256;++i){uint32_t c=i;for(int n=0;n<8;++n)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));result[i]=c;}
    return result;
  }();
  uint32_t value=~0u;for(size_t i=0;i<size;++i)value=table[(value^bytes[i])&255]^(value>>8);return value^~0u;
}
JsfxTransfer::JsfxTransfer(std::shared_ptr<Budget> budget,std::string identifier,size_t total,bool upload,std::shared_ptr<const Bytes> snapshot)
  :id(std::move(identifier)),hash_(new Hash),budget_(std::move(budget)),total_(total),allocation_(total+3*chunk_bytes),upload_(upload),snapshot_(std::move(snapshot)) {
  if(id.size()!=64 || total>32*1024*1024 || (!upload && snapshot_ && snapshot_->size()!=total))
    throw Error("INVALID_ARGUMENT", "Invalid JSFX resource transfer dimensions");
  reserve(budget_,allocation_);
  try {if(!snapshot_){data_=std::make_shared<Bytes>();data_->reserve(total);if(!upload_)snapshot_=data_;}}
  catch(...) {release(budget_,allocation_);throw;}
}
JsfxTransfer::~JsfxTransfer(){release(budget_,allocation_);}
bool JsfxTransfer::closed() const {std::lock_guard<std::mutex> lock(mutex_);return closed_;}
void JsfxTransfer::close(){std::lock_guard<std::mutex> lock(mutex_);closed_=true;}
bool JsfxTransfer::expired() const {std::lock_guard<std::mutex> lock(mutex_);return std::chrono::steady_clock::now()-touched_>std::chrono::seconds(30);}
void JsfxTransfer::append_snapshot(const Bytes& bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_ || upload_ || !data_ || bytes.size()>chunk_bytes || bytes.size()>total_-data_->size())
    throw Error("PROTOCOL_MISMATCH", "Invalid JSFX download snapshot chunk");
  data_->insert(data_->end(),bytes.begin(),bytes.end());hash_->add(bytes.data(),bytes.size());
  touched_=std::chrono::steady_clock::now();
}
bool JsfxTransfer::complete() const {
  std::lock_guard<std::mutex> lock(mutex_);return !closed_ && (upload_?data_->size():snapshot_->size())==total_;
}
std::string JsfxTransfer::checksum() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_ || (upload_?data_->size():snapshot_->size())!=total_)throw Error("FX_NOT_READY", "JSFX resource snapshot is incomplete");
  return data_?hash_->digest():sha256(*snapshot_);
}
std::shared_ptr<const JsfxTransfer::Bytes> JsfxTransfer::seal(const std::string& checksum) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_)throw Error("TRANSFER_NOT_FOUND", "The JSFX resource transfer has closed");
  if(!upload_ || data_->size()!=total_)throw Error("INVALID_ARGUMENT", "JSFX resource upload is incomplete");
  if(checksum!=hash_->digest())throw Error("INVALID_ARGUMENT", "JSFX resource SHA-256 does not match");
  sealed_=true;touched_=std::chrono::steady_clock::now();return data_;
}
JsfxTransfer::Bytes JsfxTransfer::exchange(const Bytes& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(message.size()<104 || message.size()>104+chunk_bytes || std::memcmp(message.data(),"RWR\1",4) ||
      message[4]!=(upload_?1:2) || message[5]!=1 || message[6] || message[7] || read(message,28,4) ||
      read(message,100,4) || !std::equal(id.begin(),id.end(),message.begin()+32))
    throw Error("PROTOCOL_MISMATCH", "Invalid JSFX resource packet");
  Bytes response(message.begin(),message.begin()+104);response[5]=2;write(response,96,0,4);
  const auto sequence=read(message,8,8),offset=read(message,16,8),length=read(message,24,4);
  const auto error=[&](const char* code) {
    response.resize(104);response[5]=3;const auto size=std::strlen(code);
    response.insert(response.end(),code,code+size);write(response,24,size,4);write(response,28,1,4);
    write(response,96,crc32(response.data()+104,size),4);return response;
  };
  if(closed_ || std::chrono::steady_clock::now()-touched_>std::chrono::seconds(30))return error("TRANSFER_NOT_FOUND");
  if(!sequence || length>chunk_bytes || offset>total_ || length>total_-offset)return error("INVALID_ARGUMENT");
  if(upload_) {
    if(sealed_)return error("INVALID_ARGUMENT");
    if(message.size()!=104+length || read(message,96,4)!=crc32(message.data()+104,size_t(length)))return error("PROTOCOL_MISMATCH");
    if(sequence==last_sequence_) {
      if(offset!=last_offset_ || length!=last_length_ || offset+length>data_->size() ||
          !std::equal(message.begin()+104,message.end(),data_->begin()+size_t(offset)))return error("INVALID_ARGUMENT");
    } else {
      if(sequence!=last_sequence_+1 || offset!=data_->size())return error("INVALID_ARGUMENT");
      data_->insert(data_->end(),message.begin()+104,message.end());hash_->add(message.data()+104,size_t(length));
    }
  } else {
    if(snapshot_->size()!=total_)return error("FX_NOT_READY");
    if(message.size()!=104 || read(message,96,4))return error("PROTOCOL_MISMATCH");
    if(sequence!=last_sequence_+1 && (sequence!=last_sequence_ || offset!=last_offset_ || length!=last_length_))return error("INVALID_ARGUMENT");
    response.insert(response.end(),snapshot_->begin()+size_t(offset),snapshot_->begin()+size_t(offset+length));
    write(response,96,crc32(response.data()+104,size_t(length)),4);
  }
  last_sequence_=sequence;last_offset_=offset;last_length_=size_t(length);touched_=std::chrono::steady_clock::now();return response;
}
}
