#include "entries_algo.h"
#include "martingale.h"
#include <cstdio>

namespace powermdg {

std::unique_ptr<IEntriesAlgo> create_entries_algo(const std::string& name) {
    if (name == "martingale") {
        return std::make_unique<MartingaleEntriesAlgo>();
    }
    std::fprintf(stderr, "Unknown entries_algo: '%s'\n", name.c_str());
    std::fprintf(stderr, "Available: martingale\n");
    return nullptr;
}

} // namespace powermdg
