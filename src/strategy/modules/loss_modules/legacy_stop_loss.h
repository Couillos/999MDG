#ifndef POWERMDG_STRATEGY_MODULES_LOSS_LEGACY_STOP_LOSS_H
#define POWERMDG_STRATEGY_MODULES_LOSS_LEGACY_STOP_LOSS_H
#include "loss_module.h"
namespace powermdg {
class LegacyStopLoss : public ILossModule {
public:
    std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const override;
    std::string name() const override { return "legacy_stop_loss"; }
};
} // namespace powermdg
#endif
