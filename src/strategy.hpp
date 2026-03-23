#pragma once
#include <pybind11/pybind11.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>

#include "market_book.hpp"
#include "types/common.hpp"
#include "types/polymarket.hpp"

class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void on_book(const BookMessage &msg) {};
  virtual void on_price_change(const PriceChangeMessage &msg) {};
  virtual void on_trade(const LastTradeMessage &msg) {};
  virtual void on_fill(const FillReport &fill) {};
  virtual void on_market_resolved(const MarketResolvedMessage &msg) {};

  void set_engine_callbacks(std::function<void(const Order &)> submit,
                            std::function<void(const MarketId &, uint64_t)> cancel) {
    submit_order_fn = submit;
    cancel_order_fn = cancel;
  }

  void set_books(const std::unordered_map<MarketId, MarketBook> *books) { books_ = books; }

  uint64_t submit_order(const OrderRequest &request) {
    uint64_t order_id = next_order_id_.fetch_add(1, std::memory_order_relaxed);
    uint64_t timestamp =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::utc_clock::now().time_since_epoch())
                                  .count());
    Order order{request.market_id, request.outcome, order_id, request.price,
                request.quantity,  request.side,    timestamp};
    if (submit_order_fn) submit_order_fn(order);
    return order_id;
  }

  void cancel_order(const MarketId &market_id, uint64_t id) {
    if (cancel_order_fn) cancel_order_fn(market_id, id);
  }

  // # YES Side Getters

  std::optional<double> get_yes_best_bid(const MarketId &market_id) const {
    auto *book = get_book(market_id);
    return book ? book->get_yes_best_bid() : std::nullopt;
  }

  std::optional<double> get_yes_best_ask(const MarketId &market_id) const {
    auto *book = get_book(market_id);
    return book ? book->get_yes_best_ask() : std::nullopt;
  }

  double get_yes_bid_depth(const MarketId &market_id, double price) const {
    auto *book = get_book(market_id);
    return book ? book->get_yes_bid_depth(price) : 0.0;
  }

  double get_yes_ask_depth(const MarketId &market_id, double price) const {
    auto *book = get_book(market_id);
    return book ? book->get_yes_ask_depth(price) : 0.0;
  }

  // # NO Side Getters

  std::optional<double> get_no_best_bid(const MarketId &market_id) const {
    auto *book = get_book(market_id);
    return book ? book->get_no_best_bid() : std::nullopt;
  }

  std::optional<double> get_no_best_ask(const MarketId &market_id) const {
    auto *book = get_book(market_id);
    return book ? book->get_no_best_ask() : std::nullopt;
  }

  double get_no_bid_depth(const MarketId &market_id, double price) const {
    auto *book = get_book(market_id);
    return book ? book->get_no_bid_depth(price) : 0.0;
  }

  double get_no_ask_depth(const MarketId &market_id, double price) const {
    auto *book = get_book(market_id);
    return book ? book->get_no_ask_depth(price) : 0.0;
  }

  std::optional<Outcome> get_outcome(const MarketId &market_id, const AssetId &asset_id) const {
    auto *book = get_book(market_id);
    return book ? book->get_outcome(asset_id) : std::nullopt;
  }

 private:
  std::function<void(const Order &)> submit_order_fn;
  std::function<void(const MarketId &, uint64_t)> cancel_order_fn;
  const std::unordered_map<MarketId, MarketBook> *books_ = nullptr;
  std::atomic<uint64_t> next_order_id_{1};

  const MarketBook *get_book(const MarketId &market_id) const {
    if (!books_) return nullptr;
    auto it = books_->find(market_id);
    if (it == books_->end()) return nullptr;
    return &it->second;
  }
};

namespace py = pybind11;

// Trampoline class for Pybind11
// Uses manual dispatch with py::return_value_policy::reference to avoid
// deep-copying message structs across the C++/Python boundary. Safe because
// messages are stack-allocated in the engine and outlive the synchronous callback.
class PyStrategy : public Strategy {
 public:
  using Strategy::Strategy;

  void on_book(const BookMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_book,
                           py::cast(msg, py::return_value_policy::reference));
  }

  void on_price_change(const PriceChangeMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_price_change,
                           py::cast(msg, py::return_value_policy::reference));
  }

  void on_trade(const LastTradeMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_trade,
                           py::cast(msg, py::return_value_policy::reference));
  }

  void on_fill(const FillReport &fill) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_fill,
                           py::cast(fill, py::return_value_policy::reference));
  }

  void on_market_resolved(const MarketResolvedMessage &msg) override {
    py::function override =
        py::get_override(static_cast<const Strategy *>(this), "on_market_resolved");
    if (override) {
      override(py::cast(msg, py::return_value_policy::reference));
    } else {
      Strategy::on_market_resolved(msg);
    }
  }
};
