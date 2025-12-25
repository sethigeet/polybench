#include "mock_exchange.hpp"

#define LOGGER_NAME "Exchange"
#include "logger.hpp"

void MockExchange::submit_order(const Order &order) {
  orders_[order.id] = order;
  LOG_INFO("Order Received: {} Price: {} Size: {} Side: {}", order.id, order.price, order.quantity,
           (order.side == Side::Bid ? "BUY" : "SELL"));
}

void MockExchange::cancel_order(uint64_t order_id) {
  if (orders_.erase(order_id)) {
    LOG_INFO("Order Cancelled: {}", order_id);
  }
}

double MockExchange::calculate_fill_price(const Order &order, const MarketTick &tick) const {
  if (!order_book_) {
    return tick.price;
  }

  // For buy orders, check available liquidity on the ask side
  // For sell orders, check available liquidity on the bid side
  if (order.side == Side::Bid) {
    // Buying: we're taking from the ask side
    auto best_ask = order_book_->get_best_ask();
    if (best_ask) {
      // Simulate slippage: if order qty exceeds depth, price worsens
      double available_depth = order_book_->get_ask_depth(*best_ask);
      if (order.quantity > available_depth && available_depth > 0) {
        // Simple slippage model: price increases proportionally to excess
        // demand
        double slippage_factor = (order.quantity - available_depth) / available_depth;
        double slippage = slippage_factor * 0.01 * (*best_ask);  // 1% per 100% excess
        return std::max(*best_ask + slippage, tick.price);
      }
      return *best_ask;
    }
  } else {
    // Selling: we're taking from the bid side
    auto best_bid = order_book_->get_best_bid();
    if (best_bid) {
      double available_depth = order_book_->get_bid_depth(*best_bid);
      if (order.quantity > available_depth && available_depth > 0) {
        double slippage_factor = (order.quantity - available_depth) / available_depth;
        double slippage = slippage_factor * 0.01 * (*best_bid);
        return std::min(*best_bid - slippage, tick.price);
      }
      return *best_bid;
    }
  }

  return tick.price;
}

std::vector<FillReport> MockExchange::match(const MarketTick &tick) {
  std::vector<FillReport> fills;
  auto it = orders_.begin();

  while (it != orders_.end()) {
    Order &order = it->second;
    bool should_fill = false;

    // Use order book if available for more realistic matching
    if (order_book_) {
      if (order.side == Side::Bid) {
        // Buy order fills if there's an ask at or below our limit price
        auto best_ask = order_book_->get_best_ask();
        if (best_ask && *best_ask <= order.price) {
          should_fill = true;
        }
      } else {
        // Sell order fills if there's a bid at or above our limit price
        auto best_bid = order_book_->get_best_bid();
        if (best_bid && *best_bid >= order.price) {
          should_fill = true;
        }
      }
    } else {
      // Fallback to tick-based matching (original logic)
      if (order.side == Side::Bid) {
        if (tick.side == Side::Ask && tick.price <= order.price) {
          should_fill = true;
        }
      } else {
        if (tick.side == Side::Bid && tick.price >= order.price) {
          should_fill = true;
        }
      }
    }

    if (should_fill) {
      double fill_price = calculate_fill_price(order, tick);
      fills.push_back({order.id, fill_price, order.quantity, tick.timestamp});
      LOG_INFO("Order Filled: {} @ {} (qty: {})", order.id, fill_price, order.quantity);
      it = orders_.erase(it);  // Full fill
    } else {
      ++it;
    }
  }

  return fills;
}