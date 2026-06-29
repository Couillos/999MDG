#ifndef POWERMDG_STRATEGY_MODULES_LOSS_ATR_STOP_H
#define POWERMDG_STRATEGY_MODULES_LOSS_ATR_STOP_H
#include "loss_module.h"
namespace powermdg {
class AtrStop : public ILossModule {
public:
    std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const override;
    std::string name() const override { return "atr_stop"; }
    DataNeed data_needs() const override { return DataNeed::MultiTimeframe; }
};
} // namespace powermdg
#endif
