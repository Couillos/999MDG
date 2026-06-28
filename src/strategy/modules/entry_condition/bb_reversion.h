#ifndef POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_BB_REVERSION_H
#define POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_BB_REVERSION_H
#include "entry_condition.h"
namespace powermdg {
class BbReversionEntryCondition : public IEntryCondition {
public:
    bool should_enter(const ModuleContext& ctx) const override;
    std::string name() const override { return "bb_reversion"; }
    DataNeed data_needs() const override { return DataNeed::Ema | DataNeed::RollingStdev; }
};
} // namespace powermdg
#endif
