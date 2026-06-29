#ifndef POWERMDG_STRATEGY_MODULES_LOSS_LEGACY_UNSTUCK_H
#define POWERMDG_STRATEGY_MODULES_LOSS_LEGACY_UNSTUCK_H
#include "loss_module.h"
namespace powermdg {
class LegacyUnstuck : public ILossModule {
public:
    std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const override;
    std::string name() const override { return "legacy_unstuck"; }
};
} // namespace powermdg
#endif
