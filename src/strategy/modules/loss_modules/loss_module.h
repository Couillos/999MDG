#ifndef POWERMDG_STRATEGY_MODULES_LOSS_MODULE_H
#define POWERMDG_STRATEGY_MODULES_LOSS_MODULE_H
#include "strategy/modules/module_context.h"
#include <memory>
#include <string>
#include <vector>
namespace powermdg {

/// Interface for loss/stop modules.
/// A loss module decides when to force-close a position (stop loss, time stop, etc.).
/// It returns close orders — strategy.cpp executes them.
/// Multiple loss modules can be active simultaneously; all are evaluated each tick.
class ILossModule {
public:
    virtual ~ILossModule() = default;
    virtual std::vector<CloseOrder> compute_loss_exits(const ModuleContext& ctx) const = 0;
    virtual std::string name() const = 0;
    virtual DataNeed data_needs() const { return DataNeed::None; }
};

/// Factory: creates loss modules by name.
std::unique_ptr<ILossModule> create_loss_module(const std::string& name);

/// Create multiple loss modules from a list of names.
std::vector<std::unique_ptr<ILossModule>> create_loss_modules(const std::vector<std::string>& names);

} // namespace powermdg
#endif
