#ifndef POWERMDG_PLOT_COLORS_H
#define POWERMDG_PLOT_COLORS_H

#include <string>
#include <vector>

namespace powermdg {
namespace plot {

/// Dark theme colors for all charts.
inline constexpr char const* BG         = "#1a1a2e";
inline constexpr char const* BORDER     = "#2d2d5e";
inline constexpr char const* GRID       = "#3a3a6e";
inline constexpr char const* TEXT       = "#e0e0e0";
inline constexpr char const* EQUITY     = "#00ff88";
inline constexpr char const* BALANCE    = "#4a9eff";
inline constexpr char const* EXPOSURE   = "#ff6b35";

/// Per-symbol PnL palette (up to 8 distinct colors).
inline constexpr char const* PALETTE[] = {
    "#00ff88", "#ff6b35", "#4a9eff", "#ffd93d",
    "#c084fc", "#ff4d6d", "#00d4aa", "#ffb347"
};

inline constexpr size_t PALETTE_SIZE = 8;

/// Returns the color for a given symbol index.
inline const char* symbol_color(size_t idx) {
    return PALETTE[idx % PALETTE_SIZE];
}

/// Generates a gnuplot "lc rgb" string for a symbol index.
inline std::string symbol_lc(size_t idx) {
    return std::string("lc rgb '") + symbol_color(idx) + "'";
}

} // namespace plot
} // namespace powermdg

#endif // POWERMDG_PLOT_COLORS_H
