#include "closes_algo.h"
#include "graduated_tp.h"
#include "mean_revert_tp.h"
#include "simple_grid.h"
#include "debug_log.h"
namespace powermdg {
std::unique_ptr<IClosesAlgo> create_closes_algo(const std::string& name) {
    if (name=="simple_grid") return std::make_unique<SimpleGridClosesAlgo>();
    if (name=="mean_revert_tp") return std::make_unique<MeanRevertTpClosesAlgo>();
    if (name=="graduated_tp") return std::make_unique<GraduatedTpClosesAlgo>();
    DEBUG_LOG("Unknown closes_algo: '%s'\nAvailable: simple_grid, mean_revert_tp, graduated_tp\n",name.c_str());
    return nullptr;
}
} // namespace powermdg
