#ifndef POWERMDG_STRATEGY_MODULES_LOSS_Z_STOP_H
#define POWERMDG_STRATEGY_MODULES_LOSS_Z_STOP_H
#include "loss_module.h"
namespace powermdg {
class ZStop : public ILossModule {
public:
    std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const override;
    std::string name() const override { return "z_stop"; }
    DataNeed data_needs() const override { return DataNeed::CandleSeries; }
};
} // namespace powermdg
#endif
