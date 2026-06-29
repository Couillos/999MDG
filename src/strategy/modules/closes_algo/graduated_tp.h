#ifndef POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_GRADUATED_TP_H
#define POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_GRADUATED_TP_H
#include "closes_algo.h"
namespace powermdg {
/// Graduated take-profit based on Z-score reversion.
/// TP1: close tp1_frac when |Z| <= tp1_z_threshold (mark tp1_fired)
/// TP2: close tp2_frac when |Z| <= tp2_z_threshold
/// TP3: close remaining with trailing ATR stop
class GraduatedTpClosesAlgo : public IClosesAlgo {
public:
    std::vector<CloseOrder> compute_closes(const ModuleContext& ctx) const override;
    std::string name() const override { return "graduated_tp"; }
    DataNeed data_needs() const override { return DataNeed::CandleSeries | DataNeed::MultiTimeframe; }
};
} // namespace powermdg
#endif
