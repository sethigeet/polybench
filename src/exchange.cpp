#include "exchange.hpp"

#define LOGGER_NAME "Exchange"
#include "logger.hpp"

DualLayerBook* Exchange::get_book(const std::string& asset_id) {
  if (!books_) return nullptr;
  auto it = books_->find(asset_id);
  if (it == books_->end()) return nullptr;
  return &it->second;
}

void Exchange::submit_order(const Order& order) {
  if (!Exchange::is_valid_price(order.price)) {
    throw PriceValidationError(fmt::format("Invalid price: {} Must be between {} and {}",
                                           order.price, polymarket::MIN_PRICE,
                                           polymarket::MAX_PRICE));
  }

  LOG_INFO("Order Received: {} Asset: {} Price: {} Size: {} Side: {}", order.id, order.asset_id,
           order.price, order.quantity, (order.side == Side::Buy ? "BUY" : "SELL"));

  auto* book = get_book(order.asset_id);
  if (!book) {
    LOG_WARN("No book for asset {}, cannot process order", order.asset_id);
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

void Exchange::cancel_order(const std::string& asset_id, uint64_t order_id) {
  auto* book = get_book(asset_id);
  if (!book) return;

  book->remove_virtual_order(order_id);
  LOG_INFO("Order Cancelled: {} (asset: {})", order_id, asset_id);
}

std::optional<FillReport> Exchange::try_fill_taker(const Order& order, uint64_t timestamp) {
  auto* book = get_book(order.asset_id);
  if (!book) return std::nullopt;

  if (order.side == Side::Buy) {
    auto best_ask = book->get_live_best_ask();
    if (best_ask && order.price >= *best_ask) {
      return FillReport{order.asset_id, order.id, *best_ask, order.quantity, timestamp};
    }
  } else {
    auto best_bid = book->get_live_best_bid();
    if (best_bid && order.price <= *best_bid) {
      return FillReport{order.asset_id, order.id, *best_bid, order.quantity, timestamp};
    }
  }
  return std::nullopt;
}

void Exchange::add_maker_order(const Order& order) {
  auto* book = get_book(order.asset_id);
  if (!book) return;

  double volume_ahead = 0.0;

  if (order.side == Side::Buy) {
    volume_ahead = book->get_live_bid_depth(order.price);
  } else {
    volume_ahead = book->get_live_ask_depth(order.price);
  }

  VirtualOrder virtual_order{order.asset_id, order.id,     order.price,    order.quantity,
                             order.side,     volume_ahead, order.timestamp};

  book->add_virtual_order(virtual_order);

  LOG_INFO("Maker Order Queued: {} @ {} (volume_ahead: {})", order.id, order.price, volume_ahead);
}

std::vector<FillReport> Exchange::process_trade(const LastTradeMessage& trade) {
  auto* book = get_book(trade.asset_id);
  if (!book) return {};

  Side maker_side = (trade.side == Side::Buy) ? Side::Sell : Side::Buy;

  LOG_DEBUG("Processing trade: asset {} @ {} size {} (taker: {})", trade.asset_id, trade.price,
            trade.size, trade.side == Side::Buy ? "BUY" : "SELL");

  return process_virtual_fills(trade.asset_id, trade.price, maker_side, trade.size,
                               trade.timestamp);
}

std::vector<FillReport> Exchange::process_virtual_fills(const std::string& asset_id, double price,
                                                        Side side, double trade_size,
                                                        uint64_t timestamp) {
  auto* book = get_book(asset_id);
  if (!book) return {};

  std::vector<FillReport> fills;
  auto& virtual_orders = book->get_virtual_orders();

  std::vector<uint64_t> to_remove;

  for (auto& order : virtual_orders) {
    if (order.asset_id == asset_id && order.price == price && order.side == side) {
      order.volume_ahead -= trade_size;

      if (order.volume_ahead <= 0) {
        fills.push_back({asset_id, order.id, price, order.quantity, timestamp});
        to_remove.push_back(order.id);
        LOG_INFO("Virtual Order Filled: {} @ {} (qty: {})", order.id, price, order.quantity);
      } else {
        LOG_DEBUG("Virtual Order {} queue update: volume_ahead now {}", order.id,
                  order.volume_ahead);
      }
    }
  }

  for (uint64_t id : to_remove) {
    book->remove_virtual_order(id);
  }

  return fills;
}
