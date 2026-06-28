#include "entry_condition.h"
#include "ema_dist_pct.h"
#include <cstdio>

namespace powermdg {

std::unique_ptr<IEntryCondition> create_entry_condition(const std::string& name) {
    if (name == "ema_dist_pct") {
        return std::make_unique<EmaDistPctEntryCondition>();
    }
    std::fprintf(stderr, "Unknown entry_condition: '%s'\n", name.c_str());
    std::fprintf(stderr, "Available: ema_dist_pct\n");
    return nullptr;
}

} // namespace powermdg
