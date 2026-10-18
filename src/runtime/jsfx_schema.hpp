#pragma once
#include "core/core.hpp"

namespace reaweb {
// Bounded, deterministic numeric encoding for the cooperating EEL SDK.
class JsfxSchema {
public:
  explicit JsfxSchema(Json schema);
  std::vector<double> encode(const Json& value) const;
  Json decode(const std::vector<double>& values) const;
  size_t capacity() const { return capacity_; }
private:
  Json schema_;
  size_t capacity_ = 0;
};
struct JsfxNativeDeclaration {
  Json value;
  std::string gmem;
  unsigned base = 0;
  int parameter = -1;
  explicit JsfxNativeDeclaration(const Json& input);
};
}
