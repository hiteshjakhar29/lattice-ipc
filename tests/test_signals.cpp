#include <gtest/gtest.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/signals/order_book.hpp"
#include "lattice/signals/rolling_window.hpp"
#include "lattice/signals/signal_engine.hpp"
#include "lattice/signals/signal_snapshot.hpp"

#include <cmath>
#include <type_traits>

using namespace lattice;
using namespace lattice::signals;

// ── Helpers ────────────────────────────────────────────────────────────────────

static FeedEvent add_bid(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Add, true, oid, price, qty);
}
static FeedEvent add_ask(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Add, false, oid, price, qty);
}
static FeedEvent modify_bid(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Modify, true, oid, price, qty);
}
static FeedEvent modify_ask(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Modify, false, oid, price, qty);
}
static FeedEvent cancel_bid(uint64_t oid, double price) {
    return make_feed_event<FeedEvent>(EventType::Cancel, true, oid, price, 0);
}
static FeedEvent cancel_ask(uint64_t oid, double price) {
    return make_feed_event<FeedEvent>(EventType::Cancel, false, oid, price, 0);
}
// is_bid=true → buyer-initiated trade, is_bid=false → seller-initiated
static FeedEvent trade_buy(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Trade, true,  oid, price, qty);
}
static FeedEvent trade_sell(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Trade, false, oid, price, qty);
}

// ── SignalSnapshot ─────────────────────────────────────────────────────────────

TEST(SignalSnapshot, TriviallyCopiable) {
    static_assert(std::is_trivially_copyable_v<SignalSnapshot>);
}

TEST(SignalSnapshot, SizeIs80) {
    static_assert(sizeof(SignalSnapshot) == 80);
}

// ── RollingWindow ──────────────────────────────────────────────────────────────

TEST(RollingWindow, InitiallyEmpty) {
    RollingWindow<double, 5> w;
    EXPECT_EQ(w.count(), 0u);
    EXPECT_FALSE(w.full());
}

TEST(RollingWindow, PushAndAccess) {
    RollingWindow<double, 3> w;
    w.push(1.0);
    w.push(2.0);
    w.push(3.0);
    EXPECT_EQ(w.count(), 3u);
    EXPECT_TRUE(w.full());
    EXPECT_NEAR(w[0], 1.0, 1e-12);  // oldest
    EXPECT_NEAR(w[2], 3.0, 1e-12);  // newest
}

TEST(RollingWindow, WrapsAround) {
    RollingWindow<double, 3> w;
    w.push(1.0); w.push(2.0); w.push(3.0);
    w.push(4.0);  // evicts 1.0
    EXPECT_EQ(w.count(), 3u);
    EXPECT_NEAR(w[0], 2.0, 1e-12);  // oldest is now 2
    EXPECT_NEAR(w[2], 4.0, 1e-12);  // newest is 4
}

TEST(RollingWindow, Mean) {
    RollingWindow<double, 4> w;
    w.push(1.0); w.push(2.0); w.push(3.0); w.push(4.0);
    EXPECT_NEAR(w.mean(), 2.5, 1e-12);
}

TEST(RollingWindow, MeanPartialFill) {
    RollingWindow<double, 10> w;
    w.push(2.0); w.push(4.0);
    EXPECT_NEAR(w.mean(), 3.0, 1e-12);
}

TEST(RollingWindow, StddevZeroForSingleElement) {
    RollingWindow<double, 5> w;
    w.push(7.0);
    EXPECT_NEAR(w.stddev(), 0.0, 1e-12);
}

TEST(RollingWindow, StddevTwoElements) {
    RollingWindow<double, 5> w;
    w.push(1.0); w.push(3.0);
    // population std: mean=2, deviations: -1,+1, var=1, std=1
    EXPECT_NEAR(w.stddev(), 1.0, 1e-12);
}

TEST(RollingWindow, Clear) {
    RollingWindow<double, 5> w;
    w.push(1.0); w.push(2.0);
    w.clear();
    EXPECT_EQ(w.count(), 0u);
    EXPECT_NEAR(w.mean(), 0.0, 1e-12);
}

// ── OrderBook ─────────────────────────────────────────────────────────────────

TEST(OrderBook, InitiallyEmpty) {
    OrderBook book;
    EXPECT_FALSE(book.has_both_sides());
    EXPECT_EQ(book.best_bid(), 0.0);
    EXPECT_EQ(book.best_ask(), 0.0);
    EXPECT_EQ(book.best_bid_qty(), 0u);
    EXPECT_EQ(book.best_ask_qty(), 0u);
}

TEST(OrderBook, AddBid) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 200));
    EXPECT_EQ(book.best_bid(),     100.0);
    EXPECT_EQ(book.best_bid_qty(), 200u);
}

TEST(OrderBook, AddAsk) {
    OrderBook book;
    book.process(add_ask(1, 101.0, 150));
    EXPECT_EQ(book.best_ask(),     101.0);
    EXPECT_EQ(book.best_ask_qty(), 150u);
}

TEST(OrderBook, BestBidIsHighest) {
    OrderBook book;
    book.process(add_bid(1, 99.0, 100));
    book.process(add_bid(2, 100.0, 200));
    book.process(add_bid(3, 98.0, 50));
    EXPECT_EQ(book.best_bid(), 100.0);
}

TEST(OrderBook, BestAskIsLowest) {
    OrderBook book;
    book.process(add_ask(1, 103.0, 100));
    book.process(add_ask(2, 101.0, 200));
    book.process(add_ask(3, 105.0, 50));
    EXPECT_EQ(book.best_ask(), 101.0);
}

TEST(OrderBook, ModifyUpdatesQty) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 200));
    book.process(modify_bid(1, 100.0, 350));
    EXPECT_EQ(book.best_bid_qty(), 350u);
}

TEST(OrderBook, CancelRemovesTopLevel) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 200));
    book.process(add_bid(2, 99.0, 100));
    book.process(cancel_bid(1, 100.0));
    EXPECT_EQ(book.best_bid(), 99.0);
    EXPECT_EQ(book.best_bid_qty(), 100u);
}

TEST(OrderBook, CancelLastLevelEmptiesSide) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 200));
    book.process(cancel_bid(1, 100.0));
    EXPECT_EQ(book.best_bid(), 0.0);
    EXPECT_EQ(book.best_bid_qty(), 0u);
    EXPECT_FALSE(book.has_both_sides());
}

TEST(OrderBook, TradeReducesQty) {
    OrderBook book;
    book.process(add_ask(1, 101.0, 300));
    book.process(trade_sell(1, 101.0, 100));
    EXPECT_EQ(book.best_ask_qty(), 200u);
}

TEST(OrderBook, TradeRemovesLevelAtZero) {
    OrderBook book;
    book.process(add_ask(1, 101.0, 100));
    book.process(trade_sell(1, 101.0, 100));
    EXPECT_EQ(book.best_ask(), 0.0);
    EXPECT_EQ(book.best_ask_qty(), 0u);
}

TEST(OrderBook, ProcessReturnsTrueOnBboChange) {
    OrderBook book;
    EXPECT_TRUE(book.process(add_bid(1, 100.0, 200)));
}

TEST(OrderBook, ProcessReturnsFalseOnDeepChange) {
    OrderBook book;
    // Establish BBO first
    book.process(add_bid(1, 100.0, 200));
    book.process(add_ask(2, 101.0, 100));
    // Modify a deep level — BBO unchanged
    book.process(add_bid(3, 99.0, 500)); // BBO true — new level but best_bid unchanged
    const bool bbo = book.process(modify_bid(3, 99.0, 600)); // deep modify
    EXPECT_FALSE(bbo);
}

TEST(OrderBook, HasBothSides) {
    OrderBook book;
    EXPECT_FALSE(book.has_both_sides());
    book.process(add_bid(1, 100.0, 100));
    EXPECT_FALSE(book.has_both_sides());
    book.process(add_ask(2, 101.0, 100));
    EXPECT_TRUE(book.has_both_sides());
}

TEST(OrderBook, Clear) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 200));
    book.process(add_ask(2, 101.0, 150));
    book.clear();
    EXPECT_FALSE(book.has_both_sides());
    EXPECT_EQ(book.best_bid(), 0.0);
    EXPECT_EQ(book.best_ask(), 0.0);
}

TEST(OrderBook, TopBidsReturnsDepth) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 300));
    book.process(add_bid(2, 99.0,  200));
    book.process(add_bid(3, 98.0,  100));
    book.process(add_bid(4, 97.0,   50));

    double   prices[3];
    uint32_t qtys[3];
    const std::size_t n = book.top_bids(prices, qtys, 3);
    EXPECT_EQ(n, 3u);
    EXPECT_NEAR(prices[0], 100.0, 1e-9);  // best bid first
    EXPECT_NEAR(prices[1],  99.0, 1e-9);
    EXPECT_NEAR(prices[2],  98.0, 1e-9);
    EXPECT_EQ(qtys[0], 300u);
    EXPECT_EQ(qtys[1], 200u);
    EXPECT_EQ(qtys[2], 100u);
}

TEST(OrderBook, TopAsksReturnsDepth) {
    OrderBook book;
    book.process(add_ask(1, 101.0, 300));
    book.process(add_ask(2, 102.0, 200));
    book.process(add_ask(3, 103.0, 100));

    double   prices[3];
    uint32_t qtys[3];
    const std::size_t n = book.top_asks(prices, qtys, 3);
    EXPECT_EQ(n, 3u);
    EXPECT_NEAR(prices[0], 101.0, 1e-9);  // best ask first
    EXPECT_NEAR(prices[1], 102.0, 1e-9);
    EXPECT_NEAR(prices[2], 103.0, 1e-9);
}

TEST(OrderBook, TopBidsShallowBook) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 500));

    double   prices[3];
    uint32_t qtys[3];
    const std::size_t n = book.top_bids(prices, qtys, 3);
    EXPECT_EQ(n, 1u);  // only 1 level available
}

// ── SignalEngine ───────────────────────────────────────────────────────────────

TEST(SignalEngine, ObiFormula) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 600));
    const auto& snap = eng.process(add_ask(2, 101.0, 400));

    const double expected_obi = (600.0 - 400.0) / (600.0 + 400.0); // 0.2
    EXPECT_NEAR(snap.obi, expected_obi, 1e-9);
}

TEST(SignalEngine, MicropriceFormula) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 400));
    const auto& snap = eng.process(add_ask(2, 101.0, 600));

    // microprice = (ask_qty*bid + bid_qty*ask) / (bid_qty+ask_qty)
    const double expected = (600.0 * 100.0 + 400.0 * 101.0) / 1000.0; // 100.4
    EXPECT_NEAR(snap.microprice, expected, 1e-9);
}

TEST(SignalEngine, EqualSidesObiIsZero) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    const auto& snap = eng.process(add_ask(2, 101.0, 300));
    EXPECT_NEAR(snap.obi, 0.0, 1e-9);
}

TEST(SignalEngine, SpreadComputed) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 200));
    const auto& snap = eng.process(add_ask(2, 101.5, 200));
    EXPECT_NEAR(snap.spread, 1.5, 1e-9);
}

TEST(SignalEngine, MidComputed) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 200));
    const auto& snap = eng.process(add_ask(2, 102.0, 200));
    EXPECT_NEAR(snap.mid_price, 101.0, 1e-9);
}

TEST(SignalEngine, ZeroDenominatorHandled) {
    SignalEngine eng;
    // Empty book — no division by zero
    FeedEvent dummy{};
    const auto& snap = eng.process(dummy);
    EXPECT_EQ(snap.obi, 0.0);
    EXPECT_EQ(snap.microprice, 0.0);
}

TEST(SignalEngine, BboUnchangedSnapshotSignalsUnchanged) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_ask(2, 101.0, 300));
    const auto snap1 = eng.last_snapshot();

    // Deep-book change — BBO unchanged
    eng.process(add_bid(3, 99.0, 500));
    eng.process(modify_bid(3, 99.0, 600));
    const auto& snap2 = eng.last_snapshot();

    EXPECT_NEAR(snap2.obi,        snap1.obi,        1e-9);
    EXPECT_NEAR(snap2.microprice, snap1.microprice, 1e-9);
    EXPECT_NEAR(snap2.spread,     snap1.spread,     1e-9);
}

TEST(SignalEngine, SequenceOfEvents) {
    SignalEngine eng;
    // Build a book, modify BBO, verify signals evolve
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));

    // Equal sides initially
    EXPECT_NEAR(eng.last_snapshot().obi, 0.0, 1e-9);

    // Increase bid qty — OBI should go positive
    eng.process(modify_bid(1, 100.0, 800));
    EXPECT_GT(eng.last_snapshot().obi, 0.0);

    // Cancel best ask — ask price moves up
    eng.process(add_ask(3, 102.0, 500));
    eng.process(cancel_ask(2, 101.0));
    EXPECT_NEAR(eng.book().best_ask(), 102.0, 1e-9);
}

TEST(SignalEngine, Reset) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_ask(2, 101.0, 300));
    eng.reset();
    EXPECT_EQ(eng.last_snapshot().obi, 0.0);
    EXPECT_FALSE(eng.book().has_both_sides());
}

// ── OFI ───────────────────────────────────────────────────────────────────────

TEST(SignalEngine_OFI, InitialOfiIsZero) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    // First BBO event: prev quantities are 0, bid arrives with 500 → OFI = (500-0) - (0-0) = 500
    // After first bid only, ask side is still 0 → delta_ask = 0-0 = 0
    EXPECT_NEAR(eng.last_snapshot().ofi, 500.0, 1e-9);
}

TEST(SignalEngine_OFI, OfiDeltaBidMinusDeltaAsk) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 300));
    // After add_ask: bid_qty still 500 (unchanged), ask_qty becomes 300
    // delta_bid = 500 - 500 = 0, delta_ask = 300 - 0 = 300 → OFI = 0 - 300 = -300
    EXPECT_NEAR(eng.last_snapshot().ofi, -300.0, 1e-9);
}

TEST(SignalEngine_OFI, OfiPositiveOnBidIncrease) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 200));
    eng.process(add_ask(2, 101.0, 200));
    // Now increase bid qty: delta_bid = 400-200 = 200, delta_ask = 200-200 = 0 → OFI = 200
    eng.process(modify_bid(1, 100.0, 400));
    EXPECT_GT(eng.last_snapshot().ofi, 0.0);
}

TEST(SignalEngine_OFI, OfiNegativeOnAskIncrease) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 200));
    eng.process(add_ask(2, 101.0, 200));
    // Increase ask qty: delta_bid=0, delta_ask=400-200=200 → OFI = -200
    eng.process(modify_ask(2, 101.0, 400));
    EXPECT_LT(eng.last_snapshot().ofi, 0.0);
}

TEST(SignalEngine_OFI, OfiWindowAccumulatesUpTo10) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 100));
    eng.process(add_ask(2, 101.0, 100));
    // Fire 12 BBO changes (modify_bid with alternating quantities)
    for (int i = 0; i < 12; ++i) {
        eng.process(modify_bid(1, 100.0, static_cast<uint32_t>(100 + i * 10)));
    }
    // Window is capped at 10; OFI is the latest delta
    EXPECT_NE(eng.last_snapshot().ofi, 0.0);  // non-trivial
}

TEST(SignalEngine_OFI, OfiZeroAfterReset) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 300));
    eng.reset();
    EXPECT_NEAR(eng.last_snapshot().ofi, 0.0, 1e-9);
}

// ── VAMP ──────────────────────────────────────────────────────────────────────

TEST(SignalEngine_VAMP, VampWithThreeLevels) {
    SignalEngine eng;
    // 3 bid levels
    eng.process(add_bid(1, 100.0, 200));
    eng.process(add_bid(2,  99.0, 100));
    eng.process(add_bid(3,  98.0,  50));
    // 3 ask levels
    eng.process(add_ask(4, 101.0, 200));
    eng.process(add_ask(5, 102.0, 100));
    eng.process(add_ask(6, 103.0,  50));

    // VAMP = sum(price*qty) / sum(qty)
    // bids: 100*200 + 99*100 + 98*50 = 20000 + 9900 + 4900 = 34800
    // asks: 101*200 + 102*100 + 103*50 = 20200 + 10200 + 5150 = 35550
    // total_qty = 200+100+50 + 200+100+50 = 700
    // VAMP = (34800 + 35550) / 700 = 70350/700 = 100.5
    EXPECT_NEAR(eng.last_snapshot().vamp, 70350.0 / 700.0, 1e-9);
}

TEST(SignalEngine_VAMP, VampWithFewerThanThreeLevels) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 400));
    eng.process(add_ask(2, 102.0, 400));
    // VAMP = (100*400 + 102*400) / 800 = (40000+40800)/800 = 80800/800 = 101.0
    EXPECT_NEAR(eng.last_snapshot().vamp, 101.0, 1e-9);
}

TEST(SignalEngine_VAMP, VampZeroOnEmptyBook) {
    SignalEngine eng;
    FeedEvent dummy{};
    eng.process(dummy);
    EXPECT_NEAR(eng.last_snapshot().vamp, 0.0, 1e-9);
}

TEST(SignalEngine_VAMP, VampUpdatesOnDeepChange) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 200));
    eng.process(add_bid(2,  99.0, 100));
    eng.process(add_ask(3, 101.0, 200));

    const double vamp_before = eng.last_snapshot().vamp;

    // Modify level 2 (deep) — BBO unchanged but VAMP should change
    eng.process(modify_bid(2, 99.0, 300));
    const double vamp_after = eng.last_snapshot().vamp;

    EXPECT_NE(vamp_before, vamp_after);
}

TEST(SignalEngine_VAMP, VampZeroAfterReset) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_ask(2, 101.0, 300));
    eng.reset();
    EXPECT_NEAR(eng.last_snapshot().vamp, 0.0, 1e-9);
}

// ── Rolling OBI ───────────────────────────────────────────────────────────────

TEST(SignalEngine_OBI, RollingMeanAfterSingleUpdate) {
    SignalEngine eng;
    // One BBO event: bid only, ask empty → denom = bid_qty, OBI = (600-0)/600 = 1.0
    eng.process(add_bid(1, 100.0, 600));
    EXPECT_NEAR(eng.last_snapshot().obi_mean, 1.0, 1e-9);
    EXPECT_NEAR(eng.last_snapshot().obi_std,  0.0, 1e-9);
}

TEST(SignalEngine_OBI, RollingMeanAndStdTwoUpdates) {
    SignalEngine eng;
    // Event 1: add_bid(100@600) → OBI = (600-0)/600 = 1.0
    eng.process(add_bid(1, 100.0, 600));
    // Event 2: add_ask(101@400) → OBI = (600-400)/1000 = 0.2
    eng.process(add_ask(2, 101.0, 400));
    // Window: [1.0, 0.2], mean = 0.6
    // population std: deviations ±0.4, var = 0.16, std = 0.4
    EXPECT_NEAR(eng.last_snapshot().obi_mean, 0.6, 1e-9);
    EXPECT_NEAR(eng.last_snapshot().obi_std,  0.4, 1e-9);
}

TEST(SignalEngine_OBI, RollingWindowCappedAt20) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    // Fire 25 BBO changes
    for (int i = 0; i < 25; ++i) {
        eng.process(modify_bid(1, 100.0, static_cast<uint32_t>(500 + i)));
    }
    // obi_mean and obi_std should be defined (not NaN/zero)
    EXPECT_FALSE(std::isnan(eng.last_snapshot().obi_mean));
    EXPECT_FALSE(std::isnan(eng.last_snapshot().obi_std));
}

TEST(SignalEngine_OBI, RollingStatsZeroAfterReset) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 600));
    eng.process(add_ask(2, 101.0, 400));
    eng.reset();
    EXPECT_NEAR(eng.last_snapshot().obi_mean, 0.0, 1e-9);
    EXPECT_NEAR(eng.last_snapshot().obi_std,  0.0, 1e-9);
}

// ── Trade Flow Imbalance ──────────────────────────────────────────────────────

TEST(SignalEngine_TFI, InitiallyHalfOnNoTrades) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_ask(2, 101.0, 300));
    EXPECT_NEAR(eng.last_snapshot().tfi, 0.5, 1e-9);
}

TEST(SignalEngine_TFI, AllBuyTrades) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_ask(2, 101.0, 1000));
    eng.process(trade_buy(3, 101.0, 100));
    eng.process(trade_buy(4, 101.0, 200));
    // buy_vol=300, sell_vol=0 → tfi=1.0
    EXPECT_NEAR(eng.last_snapshot().tfi, 1.0, 1e-9);
}

TEST(SignalEngine_TFI, AllSellTrades) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 1000));
    eng.process(add_ask(2, 101.0, 300));
    eng.process(trade_sell(3, 100.0, 100));
    eng.process(trade_sell(4, 100.0, 200));
    // buy_vol=0, sell_vol=300 → tfi=0.0
    EXPECT_NEAR(eng.last_snapshot().tfi, 0.0, 1e-9);
}

TEST(SignalEngine_TFI, MixedTrades) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 1000));
    eng.process(add_ask(2, 101.0, 1000));
    eng.process(trade_buy(3,  101.0, 300));
    eng.process(trade_sell(4, 100.0, 100));
    // buy_vol=300, sell_vol=100, total=400 → tfi=0.75
    EXPECT_NEAR(eng.last_snapshot().tfi, 0.75, 1e-9);
}

TEST(SignalEngine_TFI, WindowLimitedTo50Trades) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 1000000));
    eng.process(add_ask(2, 101.0, 1000000));
    // Push 60 sell trades then 1 buy trade
    for (int i = 0; i < 60; ++i) {
        eng.process(trade_sell(static_cast<uint64_t>(10 + i), 100.0, 100));
    }
    eng.process(trade_buy(200, 101.0, 600));
    // Window holds last 50: 49 sells (qty 100 each = 4900) + 1 buy (qty 600)
    // tfi = 600 / (600 + 4900) = 600/5500
    EXPECT_NEAR(eng.last_snapshot().tfi, 600.0 / 5500.0, 1e-6);
}

TEST(SignalEngine_TFI, TfiZeroPointFiveAfterReset) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 1000));
    eng.process(add_ask(2, 101.0, 1000));
    eng.process(trade_buy(3, 101.0, 500));
    eng.reset();
    EXPECT_NEAR(eng.last_snapshot().tfi, 0.5, 1e-9);
}
