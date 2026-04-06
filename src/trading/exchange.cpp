#include "trading/exchange.hpp"

#define LOGGER_NAME "Exchange"
#include "utils/logger.hpp"
#include "types/small_vector.hpp"

MarketBook* Exchange::get_book(const MarketId& market_id) {
  if (!books_) [[unlikely]] return nullptr;
  auto it = books_->find(market_id);
  if (it == books_->end()) [[unlikely]] return nullptr;
  return &it->second;
}

std::optional<FillReport> Exchange::submit_order(const Order& order) {
  if (!polymarket::is_valid_price(order.price)) {
    throw PriceValidationError(fmt::format("Invalid price: {} Must be between {} and {}",
                                           order.price, polymarket::MIN_PRICE,
                                           polymarket::MAX_PRICE));
  }

  LOG_DEBUG("Order Received: {} Market: {} Outcome: {} Price: {} Size: {} Side: {}", order.id,
            order.market_id, (order.outcome == Outcome::Yes ? "YES" : "NO"), order.price,
            order.quantity, (order.side == Side::Buy ? "BUY" : "SELL"));

  auto* book = get_book(order.market_id);
  if (!book) [[unlikely]] {
    LOG_WARN("No book for market {}, cannot process order", order.market_id);
    return std::nullopt;
  }

  // Check if this order can be filled immediately (ie. taker)
  if (auto fill = try_fill_taker(order)) {
    LOG_DEBUG("Taker Order Filled: {} @ {} (qty: {})", fill->order_id, fill->filled_price,
              fill->filled_quantity);
    return fill;
  }

  // Not immediately fillable - add as maker order
  add_maker_order(order);
  return std::nullopt;
}

void Exchange::cancel_order(const MarketId& market_id, uint64_t order_id) {
  auto* book = get_book(market_id);
  if (!book) return;

  book->remove_virtual_order(market_id, order_id);
  LOG_DEBUG("Order Cancelled: {} (market: {})", order_id, market_id);
}

std::optional<FillReport> Exchange::try_fill_taker(const Order& order) {
  auto* book = get_book(order.market_id);
  if (!book) [[unlikely]] return std::nullopt;

  double complement = polymarket::complement_price(order.price);

  if (order.side == Side::Buy) {
    if (order.outcome == Outcome::Yes) {
      // BUY YES @ p: match with SELL YES @ p or BUY NO @ (1-p)
      auto yes_ask = book->get_yes_best_ask();
      auto no_bid = book->get_no_best_bid();

      // Prefer same-outcome match, then complement match
      if (yes_ask && *yes_ask <= order.price) {
        return FillReport{order.market_id, Outcome::Yes,    order.id,  *yes_ask,
                          order.quantity,  order.timestamp, order.side};
      }
      if (no_bid && *no_bid >= complement) {
        return FillReport{order.market_id, Outcome::Yes,    order.id,  order.price,
                          order.quantity,  order.timestamp, order.side};
      }
    } else {
      // BUY NO @ p: match with SELL NO @ p or BUY YES @ (1-p)
      auto no_ask = book->get_no_best_ask();
      auto yes_bid = book->get_yes_best_bid();

      if (no_ask && *no_ask <= order.price) {
        return FillReport{order.market_id, Outcome::No,     order.id,  *no_ask,
                          order.quantity,  order.timestamp, order.side};
      }
      if (yes_bid && *yes_bid >= complement) {
        return FillReport{order.market_id, Outcome::No,     order.id,  order.price,
                          order.quantity,  order.timestamp, order.side};
      }
    }
  } else {
    if (order.outcome == Outcome::Yes) {
      // SELL YES @ p: match with BUY YES @ p or SELL NO @ (1-p)
      auto yes_bid = book->get_yes_best_bid();
      auto no_ask = book->get_no_best_ask();

      if (yes_bid && *yes_bid >= order.price) {
        return FillReport{order.market_id, Outcome::Yes,    order.id,  *yes_bid,
                          order.quantity,  order.timestamp, order.side};
      }
      if (no_ask && *no_ask <= complement) {
        return FillReport{order.market_id, Outcome::Yes,    order.id,  order.price,
                          order.quantity,  order.timestamp, order.side};
      }
    } else {
      // SELL NO @ p: match with BUY NO @ p or SELL YES @ (1-p)
      auto no_bid = book->get_no_best_bid();
      auto yes_ask = book->get_yes_best_ask();

      if (no_bid && *no_bid >= order.price) {
        return FillReport{order.market_id, Outcome::No,     order.id,  *no_bid,
                          order.quantity,  order.timestamp, order.side};
      }
      if (yes_ask && *yes_ask <= complement) {
        return FillReport{order.market_id, Outcome::No,     order.id,  order.price,
                          order.quantity,  order.timestamp, order.side};
      }
    }
  }
  return std::nullopt;
}

void Exchange::add_maker_order(const Order& order) {
  auto* book = get_book(order.market_id);
  if (!book) return;

  double volume_ahead = 0.0;
  double complement = polymarket::complement_price(order.price);

  // Calculate volume ahead from both matching sources
  if (order.side == Side::Buy) {
    if (order.outcome == Outcome::Yes) {
      // BUY YES waits behind: SELL YES at order.price and BUY NO at complement
      volume_ahead = book->get_yes_ask_depth(order.price) + book->get_no_bid_depth(complement);
    } else {
      // BUY NO waits behind: SELL NO at order.price and BUY YES at complement
      volume_ahead = book->get_no_ask_depth(order.price) + book->get_yes_bid_depth(complement);
    }
  } else {
    if (order.outcome == Outcome::Yes) {
      // SELL YES waits behind: BUY YES at order.price and SELL NO at complement
      volume_ahead = book->get_yes_bid_depth(order.price) + book->get_no_ask_depth(complement);
    } else {
      // SELL NO waits behind: BUY NO at order.price and SELL YES at complement
      volume_ahead = book->get_no_bid_depth(order.price) + book->get_yes_ask_depth(complement);
    }
  }

  VirtualOrder virtual_order{order.market_id, order.outcome, order.id,     order.price,
                             order.quantity,  order.side,    volume_ahead, order.timestamp};

  book->add_virtual_order(virtual_order);

  LOG_DEBUG("Maker Order Queued: {} @ {} (outcome: {}, volume_ahead: {})", order.id, order.price,
            (order.outcome == Outcome::Yes ? "YES" : "NO"), volume_ahead);
}

SmallVector<FillReport, 8> Exchange::process_trade(const LastTradeMessage& trade) {
  auto* book = get_book(trade.market);
  if (!book) return {};

  auto outcome_opt = book->get_outcome(trade.asset_id);
  if (!outcome_opt) {
    LOG_WARN("Unknown asset_id {} in trade, cannot process", trade.asset_id);
    return {};
  }
  Outcome outcome = *outcome_opt;

  LOG_DEBUG("Processing trade: market {} outcome {} @ {} size {} (taker: {})", trade.market,
            (outcome == Outcome::Yes ? "YES" : "NO"), trade.price, trade.size,
            trade.side == Side::Buy ? "BUY" : "SELL");

  return process_virtual_fills(trade.market, outcome, trade.price, trade.side, trade.size,
                               trade.timestamp);
}

SmallVector<FillReport, 8> Exchange::process_virtual_fills(const MarketId& market_id,
                                                           Outcome outcome, double price,
                                                           Side taker_side, double trade_size,
                                                           uint64_t timestamp) {
  auto* book = get_book(market_id);
  if (!book) return {};

  if (!book->has_virtual_orders(market_id)) return {};

  SmallVector<FillReport, 8> fills;
  SmallVector<uint64_t, 8> to_remove;

  auto& virtual_orders = book->get_virtual_orders(market_id);
  double complement = polymarket::complement_price(price);

  for (auto& order : virtual_orders) {
    std::optional<double> fill_price = std::nullopt;
    if (order.outcome == outcome && order.side != taker_side && order.price <= price) {
      fill_price = order.price;
    } else if (order.outcome != outcome && order.side == taker_side && order.price >= complement) {
      fill_price = 1 - order.price;
    }

    if (fill_price) {
      order.volume_ahead -= trade_size;

      if (order.volume_ahead <= 0) {
        fills.push_back({
            order.market_id,
            order.outcome,
            order.id,
            *fill_price,
            order.quantity,
            timestamp,
            order.side,
        });
        to_remove.push_back(order.id);
        LOG_DEBUG("Virtual Order Filled: {} @ {} (outcome: {}, qty: {})", order.id, *fill_price,
                  (order.outcome == Outcome::Yes ? "YES" : "NO"), order.quantity);
      } else {
        LOG_DEBUG("Virtual Order {} queue update: volume_ahead now {}", order.id,
                  order.volume_ahead);
      }
    }
  }

  if (!to_remove.empty()) {
    book->remove_virtual_orders(market_id,
                                std::span<const uint64_t>(to_remove.data(), to_remove.size()));
  }

  return fills;
}
