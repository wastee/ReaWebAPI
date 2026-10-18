#pragma once
#include "runtime/jsfx_native.hpp"

namespace reaweb {
class JsfxControl {
public:
  struct Context { int window; uint64_t document; std::string session; std::string origin; };
  using UndoBegin = std::function<bool(const std::string&)>;
  using UndoEnd = std::function<void()>;
  explicit JsfxControl(std::shared_ptr<JsfxNativeHub::Node> node);
  ~JsfxControl();
  // Returns true when the EEL runtime changed and the caller must replace sessionId.
  bool tick(JsfxBinding& binding, const UndoBegin& begin, const UndoEnd& end);
  Json capabilities(const std::string& session) const;
  Json call(const std::string& method, const Json& payload, const Context& context);
  Json diagnostics() const;
  void close_window(int window);
  std::vector<std::pair<std::string,Json>> events();
  int parameter() const;
  bool idle() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
