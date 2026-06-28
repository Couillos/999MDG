#ifndef POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_DCA_LINEAR_H
#define POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_DCA_LINEAR_H
#include "entries_algo.h"
namespace powermdg {
class DcaLinearEntriesAlgo : public IEntriesAlgo {
public:
    std::vector<EntryOrder> compute_entries(const ModuleContext& ctx) const override;
    std::string name() const override { return "dca_linear"; }
};
} // namespace powermdg
#endif
