#ifndef MARTINGALE_UI_TUI_H
#define MARTINGALE_UI_TUI_H

#include "config/types.h"
#include "metrics/types.h"
#include "optimizer/types.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace martingale {

/// Real-time ncurses TUI for displaying optimization progress.
/// Shows top 25 candidates sorted by score, with scoring metrics and limits.
class OptimizerTUI {
public:
    OptimizerTUI(const std::vector<ScoringMetric>& scoring,
                 const std::map<std::string, Limit>& limits,
                 size_t total_combos);
    ~OptimizerTUI();

    /// Push a completed run result (thread-safe).
    void push_result(const RunResult& result);

    /// Signal that optimization is done.
    void finish();

    /// Returns true if user pressed 'q' to abort.
    bool should_abort() const { return abort_; }

private:
    std::vector<ScoringMetric> scoring_;
    std::map<std::string, Limit> limits_;
    size_t total_combos_;
    std::atomic<size_t> completed_{0};
    std::atomic<bool> abort_{false};
    std::atomic<bool> done_{false};

    std::mutex mutex_;
    std::vector<RunResult> results_;
    std::condition_variable cv_;

    void display_loop();
    void draw_ui();
    std::string metric_value(const Metrics& m, const std::string& name) const;
};

} // namespace martingale

#endif // MARTINGALE_UI_TUI_H
