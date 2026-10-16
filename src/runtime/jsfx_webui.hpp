#pragma once
#include "platform/shared/jsfx_editor.hpp"
#include <optional>

namespace reaweb {
struct JsfxAssociation {
  fs::path entry;
  std::string ident;
  int width = 640, height = 420;
};
std::optional<JsfxAssociation> jsfx_association(const fs::path& effects, const std::string& ident);

class JsfxBinding {
  Host& host_;
  void* project_ = nullptr;
  void* track_ = nullptr;
  Guid track_guid_{}, fx_guid_{};
  uint64_t generation_ = 0;
  int index_ = -1;
  std::string ident_;
public:
  JsfxBinding(Host& host, void* track, int fx);
  int resolve();
  bool editor_matches(const JsfxEditor& editor);
  std::string ident();
  Json state();
  Json parameter(int param, bool metadata = false);
  int parameter_count();
  void set(int param, double value, bool normalized);
  void end(int param);
  void* project() const { return project_; }
  void* track() const { return track_; }
  const Guid& guid() const { return fx_guid_; }
};
}
