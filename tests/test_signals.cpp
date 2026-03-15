#include <gtest/gtest.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/signals/order_book.hpp"
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
static FeedEvent trade_ev(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Trade, false, oid, price, qty);
}

// ── SignalSnapshot ─────────────────────────────────────────────────────────────

TEST(SignalSnapshot, TriviallyCopiable) {
    static_assert(std::is_trivially_copyable_v<SignalSnapshot>);
}

TEST(SignalSnapshot, SizeIs40) {
    static_assert(sizeof(SignalSnapshot) == 40);
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
    book.process(trade_ev(1, 101.0, 100));
    EXPECT_EQ(book.best_ask_qty(), 200u);
}

TEST(OrderBook, TradeRemovesLevelAtZero) {
    OrderBook book;
    book.process(add_ask(1, 101.0, 100));
    book.process(trade_ev(1, 101.0, 100));
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
