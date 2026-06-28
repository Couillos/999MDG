#ifndef POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_MEAN_REVERT_TP_H
#define POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_MEAN_REVERT_TP_H
#include "closes_algo.h"
namespace powermdg {
class MeanRevertTpClosesAlgo : public IClosesAlgo {
public:
    std::vector<CloseOrder> compute_closes(const ModuleContext& ctx) const override;
    std::string name() const override { return "mean_revert_tp"; }
    DataNeed data_needs() const override { return DataNeed::Ema; }
};
} // namespace powermdg
#endif
