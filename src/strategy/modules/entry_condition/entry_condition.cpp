#include "entry_condition.h"
#include "bb_reversion.h"
#include "ema_dist_pct.h"
#include "zscore_ou.h"
#include "debug_log.h"
namespace powermdg {
std::unique_ptr<IEntryCondition> create_entry_condition(const std::string& name) {
    if (name=="ema_dist_pct") return std::make_unique<EmaDistPctEntryCondition>();
    if (name=="bb_reversion") return std::make_unique<BbReversionEntryCondition>();
    if (name=="zscore_ou") return std::make_unique<ZscoreOuEntryCondition>();
    DEBUG_LOG("Unknown entry_condition: '%s'\nAvailable: ema_dist_pct, bb_reversion, zscore_ou\n",name.c_str());
    return nullptr;
}
} // namespace powermdg
