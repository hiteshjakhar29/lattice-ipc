#include "lattice/anomaly/symbol_scorer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace lattice::anomaly {

SymbolScorer::SymbolScorer(const ScorerConfig& cfg)
    : cfg_(cfg)
    , mask_(cfg.max_symbols - 1)
    , table_(cfg.max_symbols)
{
    assert((cfg.max_symbols & mask_) == 0 &&
           "ScorerConfig::max_symbols must be a power of two");
}

double SymbolScorer::apply_decay(double s, uint64_t last_ns, uint64_t now_ns) const noexcept {
    if (s <= 0.0 || now_ns <= last_ns) return s;
    const double elapsed_ns = static_cast<double>(now_ns - last_ns);
    // score *= 0.5 ^ (elapsed / half_life)
    const double exponent = -elapsed_ns / cfg_.half_life_ns * 0.693147180559945; // ln(2)
    return s * std::exp(exponent);
}

SymbolScore* SymbolScorer::find_or_insert(uint16_t symbol_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(symbol_id) & mask_;
    for (uint32_t i = 0; i < static_cast<uint32_t>(table_.size()); ++i) {
        SymbolScore& slot = table_[idx];
        if (!slot.is_valid()) {
            slot.symbol_id      = symbol_id;
            slot.valid          = true;
            slot.score          = 0.0;
            slot.last_update_ns = 0;
            return &slot;
        }
        if (slot.symbol_id == symbol_id) return &slot;
        idx = (idx + 1) & mask_;
    }
    // Table full — overwrite home slot
    SymbolScore& home = table_[static_cast<uint32_t>(symbol_id) & mask_];
    home.symbol_id      = symbol_id;
    home.valid          = true;
    home.score          = 0.0;
    home.last_update_ns = 0;
    return &home;
}

void SymbolScorer::record_alert(uint16_t symbol_id, uint64_t now_ns) noexcept {
    SymbolScore* s = find_or_insert(symbol_id);
    s->score = apply_decay(s->score, s->last_update_ns, now_ns);
    s->score = std::min(1.0, s->score + cfg_.alert_increment);
    s->last_update_ns = now_ns;
}

double SymbolScorer::score(uint16_t symbol_id, uint64_t now_ns) noexcept {
    SymbolScore* s = find_or_insert(symbol_id);
    if (s->score == 0.0) return 0.0;
    return apply_decay(s->score, s->last_update_ns, now_ns);
}

void SymbolScorer::reset() noexcept {
    for (auto& slot : table_) slot.invalidate();
}

} // namespace lattice::anomaly
