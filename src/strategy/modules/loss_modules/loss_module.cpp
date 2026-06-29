#include "loss_module.h"
#include "atr_stop.h"
#include "legacy_stop_loss.h"
#include "legacy_unstuck.h"
#include "time_stop.h"
#include "z_stop.h"
#include "debug_log.h"
namespace powermdg {

std::unique_ptr<ILossModule> create_loss_module(const std::string& name) {
    if (name == "legacy_stop_loss") return std::make_unique<LegacyStopLoss>();
    if (name == "legacy_unstuck") return std::make_unique<LegacyUnstuck>();
    if (name == "z_stop") return std::make_unique<ZStop>();
    if (name == "atr_stop") return std::make_unique<AtrStop>();
    if (name == "time_stop") return std::make_unique<TimeStop>();
    DEBUG_LOG("Unknown loss_module: '%s'\nAvailable: legacy_stop_loss, legacy_unstuck, z_stop, atr_stop, time_stop\n", name.c_str());
    return nullptr;
}

std::vector<std::unique_ptr<ILossModule>> create_loss_modules(const std::vector<std::string>& names) {
    std::vector<std::unique_ptr<ILossModule>> modules;
    for (auto const& name : names) {
        auto m = create_loss_module(name);
        if (m) modules.push_back(std::move(m));
    }
    return modules;
}

} // namespace powermdg
