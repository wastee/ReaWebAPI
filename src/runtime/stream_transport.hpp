#pragma once
#include <memory>
#include <string>
#include "core/core.hpp"
namespace reaweb {
class StreamHub;
class StreamTransport {
public:
  explicit StreamTransport(StreamHub& hub);
  ~StreamTransport();
  std::string url() const;
  Json diagnostics() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
