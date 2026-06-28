#include "closes_algo.h"
#include "mean_revert_tp.h"
#include "simple_grid.h"
#include <cstdio>
namespace powermdg {
std::unique_ptr<IClosesAlgo> create_closes_algo(const std::string& name) {
    if (name == "simple_grid") return std::make_unique<SimpleGridClosesAlgo>();
    if (name == "mean_revert_tp") return std::make_unique<MeanRevertTpClosesAlgo>();
    std::fprintf(stderr, "Unknown closes_algo: '%s'\nAvailable: simple_grid, mean_revert_tp\n", name.c_str());
    return nullptr;
}
} // namespace powermdg
