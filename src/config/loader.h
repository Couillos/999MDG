#ifndef POWERMDG_CONFIG_LOADER_H
#define POWERMDG_CONFIG_LOADER_H

#include "types.h"
#include <string>

namespace powermdg {

/// Loads and validates a JSON config file, returns a fully populated Config struct.
Config load_config(const std::string& path, Mode mode);

}  // namespace powermdg

#endif  // POWERMDG_CONFIG_LOADER_H
