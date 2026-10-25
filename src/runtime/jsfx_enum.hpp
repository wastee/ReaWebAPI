#pragma once
#include "core/core.hpp"
#include "runtime/jsfx_engine_api.hpp"

namespace reaweb {
// Reads only the loaded instance. No parameter writes, file parsing or VM creation.
void jsfx_enum_metadata(const JsfxEngineApi&, void* instance, Json& parameters);
}
