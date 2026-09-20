
#include <cstddef>
#include <cstdint>
#include <list>
#include <iostream>
#include <map>
#include <cstdlib>
#include <numeric>
#include <optional> // Required for std::optional and std::nullopt
#include <random>   // Required for std::mt19937
#include <unordered_map>
#include <chrono>
#include <algorithm>

enum class Side { Buy, Sell };
struct Order {
    uint64_t id;
    Side side;
    int64_t price_ticks;
    uint32_t quantity;
};
struct OrderLocation {
    Side side;
    int64_t price_ticks;
    std::list<Order>::iterator it;
};
enum class Action { Add, Cancel, Match };
struct Event {
    Action action;
    Order order;        // used for Add and Match
    uint64_t cancel_id; // used for Cancel
};
std::vector<Event> generate(size_t n, uint32_t seed){
    std::vector<Event> events;
    events.reserve(n);
    std::vector<uint64_t> live_ids;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> action_roll(0, 99);
    std::uniform_int_distribution<int> side_roll(0, 1);
    std::uniform_int_distribution<uint32_t> q(1, 500);
    std::uniform_int_distribution<int64_t> price_offset(0, 100);
    const int64_t mid = 10100;

    for(uint64_t i = 1; i <= n;i++){

        int random_num = action_roll(gen);
        if (random_num >= 0 && random_num <= 59){

            int gen_side = side_roll(gen);
            Side side;
            if (gen_side == 0){
                side = Side::Sell;
            }
            else{
                side = Side::Buy;
            }

            uint32_t quantity = q(gen);


            int64_t price = (side == Side::Buy) ? mid - 1 - price_offset(gen)
                                    : mid + 1 + price_offset(gen);
            events.push_back({Action::Add, {i, side, price, quantity}, i});
            live_ids.push_back(i);
        }
        else if (random_num >= 60 && random_num <= 89){
            if (!live_ids.empty()) {
                std::uniform_int_distribution<size_t> idx_roll(0, live_ids.size() - 1);
                size_t idx = idx_roll(gen);
                uint64_t id = live_ids[idx];

                live_ids[idx] = live_ids.back();
                live_ids.pop_back();
                events.push_back({Action::Cancel, {}, id});
            }
            else{
                --i; continue;
            }
        }
        else{
            Side side = (side_roll(gen) == 0) ? Side::Buy : Side::Sell;
            // price through the touch so it crosses whatever is resting
            int64_t price = (side == Side::Buy) ? mid + 100 : mid - 100;
            uint32_t quantity = q(gen);
            events.push_back({Action::Match, {i, side, price, quantity}, 0});

        }
    }
    return events;
}

int failures {0};
class FlatOrderBook {
public:
    static constexpr int64_t MIN_PRICE = 9900;
    static constexpr int64_t MAX_PRICE = 10300;
    static constexpr int64_t NUM_LEVELS = MAX_PRICE - MIN_PRICE + 1;

    explicit FlatOrderBook(bool log_trades = true)
        : ask_levels_(NUM_LEVELS), bid_levels_(NUM_LEVELS), log_trades_(log_trades) {
        index_.reserve(2'000'000);
    }

    void add_order(Order o) {
        if (o.side == Side::Buy) { match_buy(o);  if (o.quantity > 0) rest(o); }
        else                     { match_sell(o); if (o.quantity > 0) rest(o); }
    }

    bool cancel_order(uint64_t id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;
        const auto& loc = found->second;
        int64_t i = idx(loc.price_ticks);
        if (loc.side == Side::Buy) {
            bid_levels_[i].erase(loc.it);
            if (bid_levels_[i].empty() && i == best_bid_idx_) advance_bid();
        } else {
            ask_levels_[i].erase(loc.it);
            if (ask_levels_[i].empty() && i == best_ask_idx_) advance_ask();
        }
        index_.erase(found);
        return true;
    }

    std::optional<int64_t> best_bid() const {
        if (best_bid_idx_ < 0) return std::nullopt;
        return price(best_bid_idx_);
    }
    std::optional<int64_t> best_ask() const {
        if (best_ask_idx_ >= NUM_LEVELS) return std::nullopt;
        return price(best_ask_idx_);
    }

    uint32_t total_quantity_at(Side side, int64_t p) const {
        if (p < MIN_PRICE || p > MAX_PRICE) return 0;
        const auto& lvl = (side == Side::Buy) ? bid_levels_[idx(p)] : ask_levels_[idx(p)];
        uint32_t t = 0; for (const Order& o : lvl) t += o.quantity; return t;
    }
    size_t order_count_at(Side side, int64_t p) const {
        if (p < MIN_PRICE || p > MAX_PRICE) return 0;
        return (side == Side::Buy) ? bid_levels_[idx(p)].size() : ask_levels_[idx(p)].size();
    }
    std::optional<uint64_t> front_order_id_at(Side side, int64_t p) const {
        if (p < MIN_PRICE || p > MAX_PRICE) return std::nullopt;
        const auto& lvl = (side == Side::Buy) ? bid_levels_[idx(p)] : ask_levels_[idx(p)];
        if (lvl.empty()) return std::nullopt;
        return lvl.front().id;
    }
    size_t level_count(Side side) const {
        size_t c = 0;
        const auto& v = (side == Side::Buy) ? bid_levels_ : ask_levels_;
        for (const auto& l : v) if (!l.empty()) c++;
        return c;
    }
    size_t index_size() const { return index_.size(); }

private:
    int64_t idx(int64_t p) const { return p - MIN_PRICE; }
    int64_t price(int64_t i) const { return i + MIN_PRICE; }

    void rest(const Order& o) {
        int64_t i = idx(o.price_ticks);
        if (o.side == Side::Buy) {
            bid_levels_[i].push_back(o);
            index_[o.id] = OrderLocation{Side::Buy, o.price_ticks, std::prev(bid_levels_[i].end())};
            if (i > best_bid_idx_) best_bid_idx_ = i;
        } else {
            ask_levels_[i].push_back(o);
            index_[o.id] = OrderLocation{Side::Sell, o.price_ticks, std::prev(ask_levels_[i].end())};
            if (i < best_ask_idx_) best_ask_idx_ = i;
        }
    }

    void advance_ask() { while (best_ask_idx_ < NUM_LEVELS && ask_levels_[best_ask_idx_].empty()) ++best_ask_idx_; }
    void advance_bid() { while (best_bid_idx_ >= 0 && bid_levels_[best_bid_idx_].empty()) --best_bid_idx_; }

    void match_buy(Order& incoming) {
        while (incoming.quantity > 0 && best_ask_idx_ < NUM_LEVELS) {
            if (price(best_ask_idx_) > incoming.price_ticks) break;
            auto& orders = ask_levels_[best_ask_idx_];
            auto& resting = orders.front();
            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if (log_trades_)
                std::cout << "TRADE " << traded << " @ " << price(best_ask_idx_)
                          << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";
            resting.quantity -= traded;
            incoming.quantity -= traded;
            if (resting.quantity == 0) { index_.erase(resting.id); orders.pop_front(); }
            if (orders.empty()) advance_ask();
        }
    }

    void match_sell(Order& incoming) {
        while (incoming.quantity > 0 && best_bid_idx_ >= 0) {
            if (price(best_bid_idx_) < incoming.price_ticks) break;
            auto& orders = bid_levels_[best_bid_idx_];
            auto& resting = orders.front();
            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if (log_trades_)
                std::cout << "TRADE " << traded << " @ " << price(best_bid_idx_)
                          << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";
            resting.quantity -= traded;
            incoming.quantity -= traded;
            if (resting.quantity == 0) { index_.erase(resting.id); orders.pop_front(); }
            if (orders.empty()) advance_bid();
        }
    }

    std::vector<std::list<Order>> ask_levels_;
    std::vector<std::list<Order>> bid_levels_;
    int64_t best_ask_idx_ = NUM_LEVELS;
    int64_t best_bid_idx_ = -1;
    std::unordered_map<uint64_t, OrderLocation> index_;
    bool log_trades_;
};
class OrderBook {
public:
explicit OrderBook(bool log_trades = true) : log_trades_(log_trades) {
    index_.reserve(2'000'000);
}
    void match_buy(Order& incoming) {
        while(incoming.quantity > 0 && !asks_.empty()){
            auto it = asks_.begin();
            if (it->first > incoming.price_ticks) break;
            auto& orders = it->second;
            auto& resting = orders.front();

            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if (log_trades_){
                std::cout << "TRADE " << traded << " @ " << it->first
                        << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";
            }
            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) {
                index_.erase(resting.id);
                orders.pop_front();}
            if (orders.empty()) {
                asks_.erase(it);              // it is discarded right afte
            }

        }
    }
    void match_sell(Order& incoming){
        while(incoming.quantity > 0 && !bids_.empty()){
            auto it = bids_.begin();
            if (it->first < incoming.price_ticks) break;
            auto& orders = it->second;
            auto& resting = orders.front();

            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if(log_trades_){
                std::cout << "TRADE " << traded << " @ " << it->first
                        << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";
            }

            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) {
                index_.erase(resting.id);
                orders.pop_front();

            }
            if (orders.empty()) {
                bids_.erase(it);              // it is discarded right afte
            }
        }
    }
    void add_order(Order o) {          // by value — we own it, we mutate it
    if (o.side == Side::Buy) {
        match_buy(o);
        if (o.quantity > 0){
            auto& level = bids_[o.price_ticks];
            level.push_back(o);
            index_[o.id] = OrderLocation{Side::Buy, o.price_ticks, std::prev(level.end())};
        }
    } else {
        match_sell(o);

        if (o.quantity > 0){
            auto& level = asks_[o.price_ticks];
            level.push_back(o);
            index_[o.id] = OrderLocation{Side::Sell, o.price_ticks, std::prev(level.end())};
        }
    }
    }
    bool cancel_order(uint64_t id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;

        const auto& loc = found->second;
        if (loc.side == Side::Buy) {
            auto level = bids_.find(loc.price_ticks);
            level->second.erase(loc.it);
            if (level->second.empty()) bids_.erase(level);
        } else {
            auto level = asks_.find(loc.price_ticks);
            level->second.erase(loc.it);
            if (level->second.empty()) asks_.erase(level);
        }
        index_.erase(found);
        return true;
    }
    bool naive_cancel_order(uint64_t id) {
        for (auto level = asks_.begin(); level != asks_.end(); ++level) {
            auto& orders = level->second;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->id == id) {
                    index_.erase(id);
                    orders.erase(it);
                    if (orders.empty()) asks_.erase(level);
                    return true;
                }
            }
        }
        for (auto level = bids_.begin(); level != bids_.end(); ++level) {
            auto& orders = level->second;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->id == id) {
                    index_.erase(id);
                    orders.erase(it);
                    if (orders.empty()) bids_.erase(level);
                    return true;
                }
            }
        }
        return false;

    }

    std::optional<int64_t> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
        }
    std::optional<int64_t> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin() -> first;
    }
    static void print_level(int64_t price, const std::list<Order>& orders) {
        uint32_t total{0};
        int count{0};
        for (const Order& o : orders) { total += o.quantity; count++; }
        std::cout << price << "  " << total << "  (" << count << ")\n";
    }
    size_t index_size(){
        return index_.size();
    }
    void print_book() const{
        for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
            const auto& price  = it->first;
            const auto& orders = it->second;
            print_level(price, orders);

        }
        std::cout << " ------------------------\n";
        for(const auto& [price,orders]  : bids_){
            print_level(price, orders);
        }
    }
    uint32_t total_quantity_at(Side side, int64_t price) const {
        uint32_t total = 0;
        if (side == Side::Sell) {
            auto it = asks_.find(price);
            if (it == asks_.end()) return 0;
            for (const Order& o : it->second) total += o.quantity;
        } else {
            auto it = bids_.find(price);
            if (it == bids_.end()) return 0;
            for (const Order& o : it->second) total += o.quantity;
        }
        return total;
    }
    size_t order_count_at(Side side, int64_t price) const {
        if (side == Side::Sell) { auto it = asks_.find(price); return it == asks_.end() ? 0 : it->second.size(); }
        auto it = bids_.find(price); return it == bids_.end() ? 0 : it->second.size();
    }
    std::optional<uint64_t> front_order_id_at(Side side, int64_t price) const {
        if (side == Side::Sell) { auto it = asks_.find(price); if (it == asks_.end() || it->second.empty()) return std::nullopt; return it->second.front().id; }
        auto it = bids_.find(price); if (it == bids_.end() || it->second.empty()) return std::nullopt; return it->second.front().id;
    }
    size_t level_count(Side side) const { return side == Side::Sell ? asks_.size() : bids_.size(); }

private:
    std::map<int64_t, std::list<Order>> asks_;
    std::map<int64_t, std::list<Order>, std::greater<>> bids_;
    std::unordered_map<uint64_t, OrderLocation> index_;
    bool log_trades_;
};

void check(bool condition, const char* name) {
    std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
    if (!condition) failures++;
}
void build(OrderBook& b) {
    b.add_order({1, Side::Sell, 10102, 300});
    b.add_order({2, Side::Sell, 10101, 200});
    b.add_order({3, Side::Sell, 10102, 200});
    b.add_order({4, Side::Buy,  10099, 300});
    b.add_order({5, Side::Buy,  10098, 400});
    b.add_order({6, Side::Buy,  10098, 350});
}

void test_partial_fill_resting() {
    std::cout << "\n-- partial fill, resting order survives --\n";
    OrderBook b;
    b.add_order({1, Side::Buy, 10099, 300});
    b.add_order({2, Side::Sell, 10099, 100});
    check(b.total_quantity_at(Side::Buy, 10099) == 200, "200 remains at the level");
    check(b.order_count_at(Side::Buy, 10099) == 1,      "still one order there");
    check(b.index_size() == 1,                          "still indexed");
    check(b.cancel_order(1) == true,                    "partially filled order is cancellable");
}

void test_partial_fill_then_cancel() {
    std::cout << "\n-- partial fill then cancel clears the level --\n";
    OrderBook b;
    b.add_order({1, Side::Buy, 10099, 300});
    b.add_order({2, Side::Sell, 10099, 100});
    check(b.cancel_order(1) == true,                  "cancel succeeds");
    check(b.total_quantity_at(Side::Buy, 10099) == 0, "nothing left at the price");
    check(b.level_count(Side::Buy) == 0,              "empty level was erased");
    check(!b.best_bid().has_value(),                  "no best bid");
    check(b.index_size() == 0,                        "index empty");
}

void test_time_priority() {
    std::cout << "\n-- time priority within a level --\n";
    OrderBook b;
    b.add_order({1, Side::Sell, 10101, 200});
    b.add_order({2, Side::Sell, 10101, 200});
    b.add_order({3, Side::Buy,  10101, 200});
    check(b.order_count_at(Side::Sell, 10101) == 1,      "one order left");
    check(b.front_order_id_at(Side::Sell, 10101) == 2u,  "order 2 survived, order 1 filled first");
    check(b.total_quantity_at(Side::Sell, 10101) == 200, "its full quantity intact");
}

void test_full_sweep() {
    std::cout << "\n-- full sweep of one side --\n";
    OrderBook b;
    b.add_order({1, Side::Sell, 10101, 200});
    b.add_order({2, Side::Sell, 10102, 300});
    b.add_order({3, Side::Buy,  10105, 500});
    check(!b.best_ask().has_value(),      "ask side empty");
    check(b.level_count(Side::Sell) == 0, "no phantom ask levels");
    check(!b.best_bid().has_value(),      "incoming fully filled, nothing rests");
    check(b.index_size() == 0,            "index empty");
}

void test_empty_book() {
    std::cout << "\n-- empty book --\n";
    OrderBook b;
    check(!b.best_bid().has_value(),        "no best bid");
    check(!b.best_ask().has_value(),        "no best ask");
    check(b.cancel_order(1) == false,       "cancel on empty book returns false");
    check(b.naive_cancel_order(1) == false, "naive cancel on empty book returns false");
    b.add_order({1, Side::Buy, 10099, 100});
    check(b.cancel_order(1) == true,        "add then cancel succeeds");
    check(b.level_count(Side::Buy) == 0,    "back to empty");
    check(b.index_size() == 0,              "index empty");
}

void test_cancel_agreement() {
    std::cout << "\n-- both cancel implementations agree --\n";
    OrderBook a, n;
    for (OrderBook* b : {&a, &n}) {
        b->add_order({1, Side::Sell, 10102, 300});
        b->add_order({2, Side::Sell, 10101, 200});
        b->add_order({3, Side::Sell, 10102, 200});
        b->add_order({4, Side::Buy,  10099, 300});
        b->add_order({5, Side::Buy,  10098, 400});
        b->add_order({6, Side::Buy,  10098, 350});
    }
    for (uint64_t id : {2u, 5u, 999u}) { a.cancel_order(id); n.naive_cancel_order(id); }

    check(a.best_bid() == n.best_bid(),     "best bid matches");
    check(a.best_ask() == n.best_ask(),     "best ask matches");
    check(a.index_size() == n.index_size(), "index size matches");
    for (int64_t p : {10098, 10099, 10101, 10102}) {
        check(a.total_quantity_at(Side::Buy, p)  == n.total_quantity_at(Side::Buy, p),  "bid quantities match");
        check(a.total_quantity_at(Side::Sell, p) == n.total_quantity_at(Side::Sell, p), "ask quantities match");
    }
}
void report(std::string name, std::vector<uint64_t> v) {
    std::sort(v.begin(), v.end());
    auto at = [&](double p) { return v[static_cast<size_t>(p * (v.size() - 1))]; };
    std::cout << name << "  n=" << v.size()
              << "  p50=" << at(0.50) << "ns"
              << "  p99=" << at(0.99) << "ns"
              << "  p99.9=" << at(0.999) << "ns"
              << "  max=" << v.back() << "ns\n";
}
template <typename Book>
void run_benchmark(size_t n, uint32_t seed,const char* label, size_t warmup = 100000) {
    std::vector<Event> events = generate(n, seed);

    Book book(false);
    std::vector<uint64_t> add_ns, cancel_ns, match_ns;
    add_ns.reserve(n);
    cancel_ns.reserve(n);
    match_ns.reserve(n);

    size_t i = 0;
    for (; i < warmup && i < events.size(); ++i) {
        const Event& e = events[i];
        if (e.action == Action::Cancel) book.cancel_order(e.cancel_id);
        else book.add_order(e.order);
    }

    for (; i < events.size(); ++i) {
        const Event& e = events[i];
        auto t0 = std::chrono::steady_clock::now();
        if (e.action == Action::Cancel) book.cancel_order(e.cancel_id);
        else book.add_order(e.order);
        auto t1 = std::chrono::steady_clock::now();

        uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        if      (e.action == Action::Add)    add_ns.push_back(ns);
        else if (e.action == Action::Cancel) cancel_ns.push_back(ns);
        else                                 match_ns.push_back(ns);
    }

    report("add   ", add_ns);
    report("cancel", cancel_ns);
    report("match ", match_ns);
}
template <typename Book>
void run_batched(size_t n, uint32_t seed, const char* label,size_t warmup = 100000, size_t batch = 100) {
    std::vector<Event> events = generate(n, seed);
    Book book(false);

    size_t i = 0;
    for (; i < warmup && i < events.size(); ++i) {
        const Event& e = events[i];
        if (e.action == Action::Cancel) book.cancel_order(e.cancel_id);
        else book.add_order(e.order);
    }

    std::vector<double> batch_ns;
    while (i + batch <= events.size()) {
        auto t0 = std::chrono::steady_clock::now();
        for (size_t j = 0; j < batch; ++j) {
            const Event& e = events[i + j];
            if (e.action == Action::Cancel) book.cancel_order(e.cancel_id);
            else book.add_order(e.order);
        }
        auto t1 = std::chrono::steady_clock::now();

        double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        batch_ns.push_back(ns / batch);
        i += batch;
    }

    std::sort(batch_ns.begin(), batch_ns.end());
    double mean = std::accumulate(batch_ns.begin(), batch_ns.end(), 0.0) / batch_ns.size();
    std::cout << "batched (n=" << batch_ns.size() << " batches of " << batch << ")"
              << "  mean=" << mean << "ns"
              << "  median=" << batch_ns[batch_ns.size() / 2] << "ns\n";
}
int main() {
    //test_partial_fill_resting();
    //test_partial_fill_then_cancel();
    //test_time_priority();
    //test_full_sweep();
    //test_empty_book();
    //test_cancel_agreement();
    //std::cout << "\n" << (failures == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    run_benchmark<OrderBook>(1'000'000, 42, "map ");
    run_benchmark<FlatOrderBook>(1'000'000, 42, "flat");
    run_batched<OrderBook>(1'000'000, 42, "map");
    run_batched<FlatOrderBook>(1'000'000, 42, "flat");
    return 0;
}


