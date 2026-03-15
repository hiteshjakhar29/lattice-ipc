#include <benchmark/benchmark.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/signals/order_book.hpp"
#include "lattice/signals/signal_engine.hpp"

using namespace lattice;
using namespace lattice::signals;

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
static FeedEvent trade_buy(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Trade, true, oid, price, qty);
}
static FeedEvent trade_sell(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Trade, false, oid, price, qty);
}

// ── OrderBook benchmarks ───────────────────────────────────────────────────────

static void BM_OrderBook_AddNewLevel(benchmark::State& state) {
    OrderBook book;
    uint64_t oid = 1;
    double   price = 100.0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(book.process(add_bid(oid++, price, 100)));
        // Cancel immediately to keep book size bounded
        book.process(cancel_bid(oid - 1, price));
        price += 0.01;
        if (price > 110.0) price = 100.0;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_AddNewLevel);

static void BM_OrderBook_ModifyExistingLevel(benchmark::State& state) {
    OrderBook book;
    // Pre-populate 10 levels
    for (int i = 0; i < 10; ++i) {
        book.process(add_bid(static_cast<uint64_t>(i), 100.0 - i * 0.5, 200));
        book.process(add_ask(static_cast<uint64_t>(100 + i), 101.0 + i * 0.5, 200));
    }
    uint32_t qty = 100;
    for (auto _ : state) {
        // Modify a deep level (price 97.5 — 5 ticks below best bid)
        benchmark::DoNotOptimize(book.process(modify_bid(5, 97.5, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_ModifyExistingLevel);

static void BM_OrderBook_ModifyTopOfBook(benchmark::State& state) {
    OrderBook book;
    book.process(add_bid(1, 100.0, 300));
    book.process(add_ask(2, 101.0, 300));
    uint32_t qty = 100;
    for (auto _ : state) {
        benchmark::DoNotOptimize(book.process(modify_bid(1, 100.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_ModifyTopOfBook);

// ── SignalEngine benchmarks ────────────────────────────────────────────────────

static void BM_SignalEngine_ProcessBboModify(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    uint32_t qty = 100;
    for (auto _ : state) {
        // BBO-changing modify — triggers full signal recompute (OBI, microprice, OFI, VAMP, rolling OBI)
        benchmark::DoNotOptimize(eng.process(modify_bid(1, 100.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_ProcessBboModify);

static void BM_SignalEngine_ProcessDeepModify(benchmark::State& state) {
    SignalEngine eng;
    // Establish BBO and deep levels
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    for (int i = 2; i < 12; ++i) {
        eng.process(add_bid(static_cast<uint64_t>(i), 100.0 - i * 0.5, 200));
    }
    uint32_t qty = 100;
    for (auto _ : state) {
        // Deep level modify — only VAMP recomputed (fast path for OBI/microprice)
        benchmark::DoNotOptimize(eng.process(modify_bid(6, 97.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_ProcessDeepModify);

static void BM_SignalEngine_MixedWorkload(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    for (int i = 2; i < 12; ++i) {
        eng.process(add_bid(static_cast<uint64_t>(i), 100.0 - i * 0.5, 200));
        eng.process(add_ask(static_cast<uint64_t>(100 + i), 101.0 + i * 0.5, 200));
    }

    // Cycle through: BBO modify, deep modify, BBO modify, deep modify...
    uint64_t iter = 0;
    uint32_t qty  = 100;
    for (auto _ : state) {
        if (iter % 2 == 0) {
            benchmark::DoNotOptimize(eng.process(modify_bid(1, 100.0, qty++)));
        } else {
            benchmark::DoNotOptimize(eng.process(modify_bid(6, 97.0, qty++)));
        }
        ++iter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_MixedWorkload);

// ── OFI benchmarks ─────────────────────────────────────────────────────────────

static void BM_SignalEngine_OFI_BboStream(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    uint32_t qty = 100;
    for (auto _ : state) {
        // Every modify triggers OFI delta + rolling window push
        benchmark::DoNotOptimize(eng.process(modify_bid(1, 100.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_OFI_BboStream);

// ── VAMP benchmarks ────────────────────────────────────────────────────────────

static void BM_SignalEngine_VAMP_ThreeLevels(benchmark::State& state) {
    SignalEngine eng;
    // Pre-populate 3 levels on each side
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_bid(2,  99.0, 200));
    eng.process(add_bid(3,  98.0, 100));
    eng.process(add_ask(4, 101.0, 300));
    eng.process(add_ask(5, 102.0, 200));
    eng.process(add_ask(6, 103.0, 100));
    uint32_t qty = 100;
    for (auto _ : state) {
        // Deep bid modify — only VAMP recomputed (BBO unchanged)
        benchmark::DoNotOptimize(eng.process(modify_bid(2, 99.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_VAMP_ThreeLevels);

static void BM_SignalEngine_VAMP_BboChange(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 300));
    eng.process(add_bid(2,  99.0, 200));
    eng.process(add_bid(3,  98.0, 100));
    eng.process(add_ask(4, 101.0, 300));
    eng.process(add_ask(5, 102.0, 200));
    eng.process(add_ask(6, 103.0, 100));
    uint32_t qty = 100;
    for (auto _ : state) {
        // BBO modify — full signal recompute including VAMP
        benchmark::DoNotOptimize(eng.process(modify_bid(1, 100.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_VAMP_BboChange);

// ── Rolling OBI benchmarks ─────────────────────────────────────────────────────

static void BM_SignalEngine_RollingOBI(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 500));
    uint32_t qty = 100;
    for (auto _ : state) {
        // Each BBO change pushes a new OBI value and recomputes mean+std over 20-element window
        benchmark::DoNotOptimize(eng.process(modify_bid(1, 100.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_RollingOBI);

// ── Trade Flow Imbalance benchmarks ───────────────────────────────────────────

static void BM_SignalEngine_TFI_BuyTrades(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 10000000));
    eng.process(add_ask(2, 101.0, 10000000));
    uint64_t oid = 100;
    uint32_t qty = 100;
    for (auto _ : state) {
        // Trade event triggers TFI recompute over 50-trade window (O(50) scan)
        benchmark::DoNotOptimize(eng.process(trade_buy(oid++, 101.0, qty++)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_TFI_BuyTrades);

static void BM_SignalEngine_TFI_MixedTrades(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 10000000));
    eng.process(add_ask(2, 101.0, 10000000));
    uint64_t oid = 100;
    uint32_t qty = 100;
    uint64_t iter = 0;
    for (auto _ : state) {
        if (iter % 2 == 0) {
            benchmark::DoNotOptimize(eng.process(trade_buy(oid++, 101.0, qty)));
        } else {
            benchmark::DoNotOptimize(eng.process(trade_sell(oid++, 100.0, qty)));
        }
        ++qty; ++iter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_TFI_MixedTrades);

static void BM_SignalEngine_TFI_WithAskModify(benchmark::State& state) {
    SignalEngine eng;
    eng.process(add_bid(1, 100.0, 500));
    eng.process(add_ask(2, 101.0, 10000000));
    uint64_t oid = 100;
    uint32_t qty = 200;
    uint64_t iter = 0;
    for (auto _ : state) {
        if (iter % 3 == 0) {
            benchmark::DoNotOptimize(eng.process(trade_buy(oid++, 101.0, qty)));
        } else if (iter % 3 == 1) {
            benchmark::DoNotOptimize(eng.process(trade_sell(oid++, 100.0, qty)));
        } else {
            // BBO modify interspersed
            benchmark::DoNotOptimize(eng.process(modify_ask(2, 101.0, qty)));
        }
        ++qty; ++iter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalEngine_TFI_WithAskModify);
