#include "closes_algo.h"
#include "simple_grid.h"
#include <cstdio>

namespace powermdg {

std::unique_ptr<IClosesAlgo> create_closes_algo(const std::string& name) {
    if (name == "simple_grid") {
        return std::make_unique<SimpleGridClosesAlgo>();
    }
    std::fprintf(stderr, "Unknown closes_algo: '%s'\n", name.c_str());
    std::fprintf(stderr, "Available: simple_grid\n");
    return nullptr;
}

} // namespace powermdg
