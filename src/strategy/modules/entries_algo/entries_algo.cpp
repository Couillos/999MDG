#include "entries_algo.h"
#include "dca_linear.h"
#include "martingale.h"
#include "debug_log.h"
namespace powermdg {
std::unique_ptr<IEntriesAlgo> create_entries_algo(const std::string& name) {
    if (name == "martingale") return std::make_unique<MartingaleEntriesAlgo>();
    if (name == "dca_linear") return std::make_unique<DcaLinearEntriesAlgo>();
    DEBUG_LOG("Unknown entries_algo: '%s'\nAvailable: martingale, dca_linear\n", name.c_str());
    return nullptr;
}
} // namespace powermdg
