
#include <cstdint>
#include <list>
#include <iostream>
#include <map>
#include <optional> // Required for std::optional and std::nullopt

enum class Side { Buy, Sell };
struct Order {
    uint64_t id;
    Side side;
    int64_t price_ticks;
    uint32_t quantity;
};
class OrderBook {
public:
    void match_buy(Order& incoming) {
        while(incoming.quantity > 0 && !asks_.empty()){
            auto it = asks_.begin();
            if (it->first > incoming.price_ticks) break;
            auto& orders = it->second;
            auto& resting = orders.front();

            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            std::cout << "TRADE " << traded << " @ " << it->first
                    << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";

            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) orders.pop_front();
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
            std::cout << "TRADE " << traded << " @ " << it->first
                    << " (resting " << resting.id << " x incoming " << incoming.id << ")\n";

            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) orders.pop_front();
            if (orders.empty()) {
                bids_.erase(it);              // it is discarded right afte
            }
        }
    }
    void add_order(Order o) {          // by value — we own it, we mutate it
    if (o.side == Side::Buy) {
        match_buy(o);
        if (o.quantity > 0) bids_[o.price_ticks].push_back(o);
    } else {
        match_sell(o);
        if (o.quantity > 0) asks_[o.price_ticks].push_back(o);
    }
    }
    bool naive_cancel_order(uint64_t id) {
        for (auto level = asks_.begin(); level != asks_.end(); ++level) {
            auto& orders = level->second;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->id == id) {
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
                    orders.erase(it);
                    if (orders.empty()) asks_.erase(level);
                    return true;
                }
            }
        }

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

private:
    std::map<int64_t, std::list<Order>> asks_;
    std::map<int64_t, std::list<Order>, std::greater<>> bids_;
};

int main() {
    OrderBook book;
    std::cout << "--- Test 1: build book, no crossing ---\n";
    book.add_order({1, Side::Sell, 10102, 300});
    book.add_order({2, Side::Sell, 10101, 200});
    book.add_order({3, Side::Sell, 10102, 200});
    book.add_order({4, Side::Buy,  10099, 300});
    book.add_order({5, Side::Buy,  10098, 400});
    book.add_order({6, Side::Buy,  10098, 350});
    book.print_book();

    std::cout << "\n--- Test 2: buy 500 @ 10102 ---\n";
    book.add_order({7, Side::Buy, 10102, 500});
    book.print_book();

    std::cout << "\n--- Test 3: sell 100 @ 10098 ---\n";
    book.add_order({8, Side::Sell, 10098, 100});
    book.print_book();

    std::cout << "\n--- Test 4: sell 2000 @ 10000 ---\n";
    book.add_order({9, Side::Sell, 10000, 2000});
    book.print_book();
    return 0;
}
