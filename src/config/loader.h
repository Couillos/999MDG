#ifndef MARTINGALE_CONFIG_LOADER_H
#define MARTINGALE_CONFIG_LOADER_H

#include "types.h"
#include <string>

namespace martingale {

/// Loads and validates a JSON config file, returns a fully populated Config struct.
Config load_config(const std::string& path, Mode mode);

}  // namespace martingale

#endif  // MARTINGALE_CONFIG_LOADER_H
