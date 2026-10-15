#pragma once
#include "core/core.hpp"

namespace reaweb {
inline Json transparency_capabilities(bool supported, const char* backend, const std::string& reason = {}, bool frameless = true) {
  return {{"supported", supported}, {"framed", supported}, {"frameless", supported && frameless},
    {"floatingOnly", true}, {"backend", backend}, {"reason", reason}};
}
inline std::string transparent_page_script(const std::string& post) {
  return ";addEventListener('load',()=>{" + post + "},{once:true});";
}
}
