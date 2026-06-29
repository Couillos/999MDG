#ifndef POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_ZSCORE_OU_H
#define POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_ZSCORE_OU_H
#include "entry_condition.h"
namespace powermdg {
/// Z-score mean-reversion entry based on Ornstein-Uhlenbeck framework.
/// Computes VWAP over lookback candles, then Z = (close - VWAP) / stdev.
/// Enters LONG when Z <= -zscore_entry_threshold.
/// Also checks ATR regime filter: ATR(14, HTF) < atr_filter_mult * median ATR.
class ZscoreOuEntryCondition : public IEntryCondition {
public:
    bool should_enter(const ModuleContext& ctx) const override;
    std::string name() const override { return "zscore_ou"; }
    DataNeed data_needs() const override { return DataNeed::CandleSeries | DataNeed::MultiTimeframe; }
};
} // namespace powermdg
#endif
