#pragma once
#include <pybind11/pybind11.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

#include "dual_layer_book.hpp"
#include "types/common.hpp"
#include "types/polymarket.hpp"

class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void on_book(const BookMessage &msg) = 0;
  virtual void on_price_change(const PriceChangeMessage &msg) = 0;
  virtual void on_trade(const LastTradeMessage &msg) = 0;
  virtual void on_fill(const FillReport &fill) = 0;

  void set_engine_callbacks(std::function<void(const Order &)> submit,
                            std::function<void(const std::string &, uint64_t)> cancel) {
    submit_order_fn = submit;
    cancel_order_fn = cancel;
  }

  void set_books(const std::unordered_map<std::string, DualLayerBook> *books) { books_ = books; }

  uint64_t submit_order(const OrderRequest &request) {
    uint64_t order_id = next_order_id_.fetch_add(1, std::memory_order_relaxed);
    uint64_t timestamp =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::utc_clock::now().time_since_epoch())
                                  .count());
    Order order{request.asset_id, order_id,     request.price,
                request.quantity, request.side, timestamp};
    if (submit_order_fn) submit_order_fn(order);
    return order_id;
  }

  void cancel_order(const std::string &asset_id, uint64_t id) {
    if (cancel_order_fn) cancel_order_fn(asset_id, id);
  }

  std::optional<double> get_best_bid(const std::string &asset_id) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_best_bid() : std::nullopt;
  }

  std::optional<double> get_best_ask(const std::string &asset_id) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_best_ask() : std::nullopt;
  }

  std::optional<double> get_mid_price(const std::string &asset_id) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_mid_price() : std::nullopt;
  }

  std::optional<double> get_spread(const std::string &asset_id) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_spread() : std::nullopt;
  }

  double get_bid_depth(const std::string &asset_id, double price) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_bid_depth(price) : 0.0;
  }

  double get_ask_depth(const std::string &asset_id, double price) const {
    auto *book = get_book(asset_id);
    return book ? book->get_live_ask_depth(price) : 0.0;
  }

 private:
  std::function<void(const Order &)> submit_order_fn;
  std::function<void(const std::string &, uint64_t)> cancel_order_fn;
  const std::unordered_map<std::string, DualLayerBook> *books_ = nullptr;
  std::atomic<uint64_t> next_order_id_{1};

  const DualLayerBook *get_book(const std::string &asset_id) const {
    if (!books_) return nullptr;
    auto it = books_->find(asset_id);
    if (it == books_->end()) return nullptr;
    return &it->second;
  }
};

// Trampoline class for Pybind11
class PyStrategy : public Strategy {
 public:
  using Strategy::Strategy;

  void on_book(const BookMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_book, msg);
  }

  void on_price_change(const PriceChangeMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_price_change, msg);
  }

  void on_trade(const LastTradeMessage &msg) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_trade, msg);
  }

  void on_fill(const FillReport &fill) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_fill, fill);
  }
};
