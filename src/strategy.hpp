#pragma once
#include <pybind11/pybind11.h>

#include <functional>

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
                            std::function<void(uint64_t)> cancel) {
    submit_order_fn = submit;
    cancel_order_fn = cancel;
  }

  void set_book(const DualLayerBook *book) { book_ = book; }

  void submit_order(const Order &order) {
    if (submit_order_fn) submit_order_fn(order);
  }

  void cancel_order(uint64_t id) {
    if (cancel_order_fn) cancel_order_fn(id);
  }

  std::optional<double> get_best_bid() const {
    return book_ ? book_->get_live_best_bid() : std::nullopt;
  }

  std::optional<double> get_best_ask() const {
    return book_ ? book_->get_live_best_ask() : std::nullopt;
  }

  std::optional<double> get_mid_price() const {
    return book_ ? book_->get_live_mid_price() : std::nullopt;
  }

  std::optional<double> get_spread() const {
    return book_ ? book_->get_live_spread() : std::nullopt;
  }

  double get_bid_depth(double price) const {
    return book_ ? book_->get_live_bid_depth(price) : 0.0;
  }

  double get_ask_depth(double price) const {
    return book_ ? book_->get_live_ask_depth(price) : 0.0;
  }

 private:
  std::function<void(const Order &)> submit_order_fn;
  std::function<void(uint64_t)> cancel_order_fn;
  const DualLayerBook *book_ = nullptr;
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
