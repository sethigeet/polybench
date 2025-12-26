#include "exchange.hpp"

#define LOGGER_NAME "Exchange"
#include "logger.hpp"

void Exchange::submit_order(const Order& order) {
  if (!Exchange::is_valid_price(order.price)) {
    std::ostringstream oss;
    oss << "Invalid price: " << order.price << ". Must be between " << polymarket::MIN_PRICE
        << " and " << polymarket::MAX_PRICE;
    throw PriceValidationError(oss.str());
  }

  LOG_INFO("Order Received: {} Price: {} Size: {} Side: {}", order.id, order.price, order.quantity,
           (order.side == Side::Buy ? "BUY" : "SELL"));

  if (!book_) {
    LOG_WARN("No book attached, cannot process order");
    return;
  }

  // Check if this order can be filled immediately (ie. taker)
  if (auto fill = try_fill_taker(order, order.timestamp)) {
    LOG_INFO("Taker Order Filled: {} @ {} (qty: {})", fill->order_id, fill->filled_price,
             fill->filled_quantity);
    return;
  }

  // Not immediately fillable - add as maker order
  add_maker_order(order);
}

void Exchange::cancel_order(uint64_t order_id) {
  if (!book_) return;

  book_->remove_virtual_order(order_id);
  LOG_INFO("Order Cancelled: {}", order_id);
}

std::optional<FillReport> Exchange::try_fill_taker(const Order& order, uint64_t timestamp) {
  if (order.side == Side::Buy) {
    auto best_ask = book_->get_live_best_ask();
    if (best_ask && order.price >= *best_ask) {
      return FillReport{order.id, *best_ask, order.quantity, timestamp};
    }
  } else {
    auto best_bid = book_->get_live_best_bid();
    if (best_bid && order.price <= *best_bid) {
      return FillReport{order.id, *best_bid, order.quantity, timestamp};
    }
  }
  return std::nullopt;
}

void Exchange::add_maker_order(const Order& order) {
  double volume_ahead = 0.0;

  if (order.side == Side::Buy) {
    volume_ahead = book_->get_live_bid_depth(order.price);
  } else {
    volume_ahead = book_->get_live_ask_depth(order.price);
  }

  VirtualOrder virtual_order{order.id,   order.price,  order.quantity,
                             order.side, volume_ahead, order.timestamp};

  book_->add_virtual_order(virtual_order);

  LOG_INFO("Maker Order Queued: {} @ {} (volume_ahead: {})", order.id, order.price, volume_ahead);
}

std::vector<FillReport> Exchange::process_trade(const LastTradeMessage& trade) {
  if (!book_) return {};

  Side maker_side = (trade.side == Side::Buy) ? Side::Sell : Side::Buy;

  LOG_DEBUG("Processing trade: asset {} @ {} size {} (taker: {})", trade.asset_id, trade.price,
            trade.size, trade.side == Side::Buy ? "BUY" : "SELL");

  return process_virtual_fills(trade.price, maker_side, trade.size, trade.timestamp);
}

std::vector<FillReport> Exchange::process_virtual_fills(double price, Side side, double trade_size,
                                                        uint64_t timestamp) {
  std::vector<FillReport> fills;
  auto& virtual_orders = book_->get_virtual_orders();

  std::vector<uint64_t> to_remove;

  for (auto& order : virtual_orders) {
    if (order.price == price && order.side == side) {
      order.volume_ahead -= trade_size;

      if (order.volume_ahead <= 0) {
        fills.push_back({order.id, price, order.quantity, timestamp});
        to_remove.push_back(order.id);
        LOG_INFO("Virtual Order Filled: {} @ {} (qty: {})", order.id, price, order.quantity);
      } else {
        LOG_DEBUG("Virtual Order {} queue update: volume_ahead now {}", order.id,
                  order.volume_ahead);
      }
    }
  }

  for (uint64_t id : to_remove) {
    book_->remove_virtual_order(id);
  }

  return fills;
}
