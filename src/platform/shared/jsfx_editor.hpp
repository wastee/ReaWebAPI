#pragma once
#include "core/core.hpp"
#include <optional>
#include <vector>

namespace reaweb {
class JsfxEditorHost {
public:
  virtual ~JsfxEditorHost() = default;
  virtual void* parent() const = 0;
  virtual bool valid() const = 0;
  virtual void resize(void* child) = 0;
  virtual void prepare() {}
  virtual void present(void* child) = 0;
  virtual void reveal() {}
  virtual void restore() = 0;
  virtual void park() { restore(); }
  virtual bool retain_during_switch() const { return false; }
  virtual std::optional<int> selected_fx() const { return {}; }
  virtual void sync_selection(int) {}
  virtual void request_size(int, int) {}
  virtual void set_background(unsigned) {}
  virtual void on_destroy(std::function<void()>) {}
};
struct JsfxEditor {
  void* track = nullptr;
  int fx = -1;
  bool floating = false;
  void* container = nullptr;
  std::shared_ptr<JsfxEditorHost> host;
};
class JsfxEditorDiscovery {
public:
  struct Size { int width, height; };
  using Match = std::function<std::optional<Size>(void*, int)>;
  virtual ~JsfxEditorDiscovery() = default;
  virtual std::vector<JsfxEditor> scan(Host&, Match) = 0;
  virtual bool pending() const { return false; }
};
using JsfxDiscovery = std::shared_ptr<JsfxEditorDiscovery>;
JsfxDiscovery make_jsfx_editor_discovery();
}
