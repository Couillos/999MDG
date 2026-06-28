#include "entries_algo.h"
#include "dca_linear.h"
#include "martingale.h"
#include <cstdio>
namespace powermdg {
std::unique_ptr<IEntriesAlgo> create_entries_algo(const std::string& name) {
    if (name == "martingale") return std::make_unique<MartingaleEntriesAlgo>();
    if (name == "dca_linear") return std::make_unique<DcaLinearEntriesAlgo>();
    std::fprintf(stderr, "Unknown entries_algo: '%s'\nAvailable: martingale, dca_linear\n", name.c_str());
    return nullptr;
}
} // namespace powermdg
