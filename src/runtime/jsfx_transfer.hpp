#pragma once
#include "core/core.hpp"
#include <mutex>
#include <atomic>

namespace reaweb {
class JsfxTransfer {
public:
  using Bytes=std::vector<unsigned char>;
  static constexpr size_t chunk_bytes=256*1024;
  struct Budget {std::atomic<size_t> bytes{0};size_t limit=128*1024*1024;std::shared_ptr<Budget> parent;};
  JsfxTransfer(std::shared_ptr<Budget> budget,std::string id,size_t total,bool upload,std::shared_ptr<const Bytes> snapshot={});
  ~JsfxTransfer();
  Bytes exchange(const Bytes& message);
  std::shared_ptr<const Bytes> seal(const std::string& checksum);
  void append_snapshot(const Bytes& bytes);
  bool complete() const;
  std::string checksum() const;
  void close();
  bool closed() const;
  bool expired() const;
  size_t total() const {return total_;}
  const std::string id;
  static std::string sha256(const Bytes& bytes);
  static uint32_t crc32(const unsigned char* bytes,size_t size);
private:
  struct Hash;
  std::unique_ptr<Hash> hash_;
  std::shared_ptr<Budget> budget_;
  size_t total_,allocation_;
  bool upload_,closed_=false,sealed_=false;
  mutable std::mutex mutex_;
  std::shared_ptr<Bytes> data_;
  std::shared_ptr<const Bytes> snapshot_;
  uint64_t last_sequence_=0,last_offset_=0;
  size_t last_length_=0;
  std::chrono::steady_clock::time_point touched_=std::chrono::steady_clock::now();
};
}
