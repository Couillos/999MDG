#ifndef POWERMDG_STRATEGY_MODULES_LOSS_TIME_STOP_H
#define POWERMDG_STRATEGY_MODULES_LOSS_TIME_STOP_H
#include "loss_module.h"
namespace powermdg {
class TimeStop : public ILossModule {
public:
    std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const override;
    std::string name() const override { return "time_stop"; }
};
} // namespace powermdg
#endif
