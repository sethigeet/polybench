#pragma once
#include <pybind11/pybind11.h>

#include "order_book.hpp"
#include "types.hpp"

class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void on_tick(const MarketTick &tick) = 0;
  virtual void on_fill(const FillReport &fill) = 0;

  void set_engine_callbacks(std::function<void(const Order &)> submit,
                            std::function<void(uint64_t)> cancel) {
    submit_order_fn = submit;
    cancel_order_fn = cancel;
  }

  void set_order_book(const OrderBook *book) { order_book_ = book; }

  void submit_order(const Order &order) {
    if (submit_order_fn) submit_order_fn(order);
  }

  void cancel_order(uint64_t id) {
    if (cancel_order_fn) cancel_order_fn(id);
  }

  std::optional<double> get_best_bid() const {
    return order_book_ ? order_book_->get_best_bid() : std::nullopt;
  }

  std::optional<double> get_best_ask() const {
    return order_book_ ? order_book_->get_best_ask() : std::nullopt;
  }

  std::optional<double> get_mid_price() const {
    return order_book_ ? order_book_->get_mid_price() : std::nullopt;
  }

  std::optional<double> get_spread() const {
    return order_book_ ? order_book_->get_spread() : std::nullopt;
  }

  double get_bid_depth(double price) const {
    return order_book_ ? order_book_->get_bid_depth(price) : 0.0;
  }

  double get_ask_depth(double price) const {
    return order_book_ ? order_book_->get_ask_depth(price) : 0.0;
  }

 private:
  std::function<void(const Order &)> submit_order_fn;
  std::function<void(uint64_t)> cancel_order_fn;
  const OrderBook *order_book_ = nullptr;
};

// Trampoline class for Pybind11
class PyStrategy : public Strategy {
 public:
  using Strategy::Strategy;

  void on_tick(const MarketTick &tick) override {
    PYBIND11_OVERRIDE_PURE(void,      // Return type
                           Strategy,  // Parent class
                           on_tick,   // Name of function in C++ (must match Python name)
                           tick       // Argument(s)
    );
  }

  void on_fill(const FillReport &fill) override {
    PYBIND11_OVERRIDE_PURE(void, Strategy, on_fill, fill);
  }
};
