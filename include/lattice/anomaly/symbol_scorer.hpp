#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

namespace lattice::anomaly {

struct ScorerConfig {
    uint32_t max_symbols        = 256;          ///< Capacity of symbol table; must be power of two
    double   half_life_ns       = 5e9;          ///< Score half-life in nanoseconds (default 5 s)
    double   alert_increment    = 0.2;          ///< Score bump per alert (clamped to 1.0)
};

/// Per-symbol suspicion score entry stored in the fixed hash table.
struct SymbolScore {
    uint16_t symbol_id      = 0;
    bool     valid          = false;
    uint8_t  _pad[5]        = {};
    double   score          = 0.0;
    uint64_t last_update_ns = 0;

    [[nodiscard]] bool is_valid()  const noexcept { return valid; }
    void               invalidate() noexcept { valid = false; symbol_id = 0; score = 0.0; }
};

/// Tracks a suspicion score in [0.0, 1.0] for each symbol.
///
/// Score increases by alert_increment each time record_alert() is called,
/// and decays exponentially with the configured half-life between calls.
///
/// Implemented as a fixed-size open-addressing hash table.
/// Zero heap allocation after construction.
class SymbolScorer {
public:
    explicit SymbolScorer(const ScorerConfig& cfg = {});

    /// Bump the score for symbol_id (with decay applied first).
    void record_alert(uint16_t symbol_id, uint64_t now_ns) noexcept;

    /// Return current (decayed) score for symbol_id. Returns 0.0 if not seen.
    [[nodiscard]] double score(uint16_t symbol_id, uint64_t now_ns) noexcept;

    void reset() noexcept;

private:
    ScorerConfig              cfg_;
    uint32_t                  mask_;
    std::vector<SymbolScore>  table_;

    SymbolScore* find_or_insert(uint16_t symbol_id) noexcept;
    [[nodiscard]] double apply_decay(double s, uint64_t last_ns, uint64_t now_ns) const noexcept;
};

} // namespace lattice::anomaly
