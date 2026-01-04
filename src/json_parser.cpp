#include "json_parser.hpp"

#include <cctype>

#include "utils.hpp"

#define LOGGER_NAME "JsonParser"
#include "logger.hpp"

std::vector<PolymarketMessage> JsonParser::parse(const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    if (j.is_array()) {
      std::vector<PolymarketMessage> messages;
      for (const auto& item : j) {
        auto parsed = parse(item.dump());
        if (parsed.size() > 0) {
          messages.insert(messages.end(), parsed.begin(), parsed.end());
        }
      }
      return messages;
    }

    if (!j.contains("event_type")) {
      LOG_WARN("No event type found");
      return {};
    }

    std::string event_type = j["event_type"].get<std::string>();

    if (event_type == "book") {
      return {parse_book_message(j)};
    } else if (event_type == "price_change") {
      return {parse_price_change_message(j)};
    } else if (event_type == "last_trade_price") {
      return {parse_last_trade_message(j)};
    } else if (event_type == "tick_size_change") {
      return {parse_tick_size_change_message(j)};
    } else if (event_type == "market_resolved") {
      return {parse_market_resolved_message(j)};
    } else if (event_type == "market_created") {
      return {};
    } else if (event_type == "best_bid_ask") {
      return {};
    } else {
      LOG_WARN("Unknown event type: {}", event_type);
      return {};
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to parse message: {}", e.what());
    return {};
  }
}

BookMessage JsonParser::parse_book_message(const nlohmann::json& j) {
  BookMessage msg;
  msg.asset_id = j["asset_id"].get<std::string>();
  msg.market = j["market"].get<std::string>();
  msg.timestamp = parse_timestamp(j["timestamp"]);

  // Parse bids
  if (j.contains("bids") && j["bids"].is_array()) {
    for (const auto& bid : j["bids"]) {
      OrderSummary order;
      order.price = parse_double(bid["price"]);
      order.size = parse_double(bid["size"]);
      msg.bids.push_back(order);
    }
  }

  // Parse asks
  if (j.contains("asks") && j["asks"].is_array()) {
    for (const auto& ask : j["asks"]) {
      OrderSummary order;
      order.price = parse_double(ask["price"]);
      order.size = parse_double(ask["size"]);
      msg.asks.push_back(order);
    }
  }

  return msg;
}

PriceChangeMessage JsonParser::parse_price_change_message(const nlohmann::json& j) {
  PriceChangeMessage msg;
  msg.market = j["market"].get<std::string>();
  msg.timestamp = parse_timestamp(j["timestamp"]);

  // Parse price_changes array
  if (j.contains("price_changes") && j["price_changes"].is_array()) {
    for (const auto& pc : j["price_changes"]) {
      PriceChange change;
      change.asset_id = pc["asset_id"].get<std::string>();
      change.price = parse_double(pc["price"]);
      change.size = parse_double(pc["size"]);
      change.side = parse_side(pc["side"].get<std::string>());
      change.best_bid = parse_double(pc["best_bid"]);
      change.best_ask = parse_double(pc["best_ask"]);
      msg.price_changes.push_back(change);
    }
  }

  return msg;
}

LastTradeMessage JsonParser::parse_last_trade_message(const nlohmann::json& j) {
  LastTradeMessage msg;
  msg.asset_id = j["asset_id"].get<std::string>();
  msg.market = j["market"].get<std::string>();
  msg.price = parse_double(j["price"]);
  msg.side = parse_side(j["side"].get<std::string>());
  msg.size = parse_double(j["size"]);
  msg.fee_rate_bps = parse_int(j["fee_rate_bps"]);
  msg.timestamp = parse_timestamp(j["timestamp"]);
  return msg;
}

TickSizeChangeMessage JsonParser::parse_tick_size_change_message(const nlohmann::json& j) {
  TickSizeChangeMessage msg;
  msg.asset_id = j["asset_id"].get<std::string>();
  msg.market = j["market"].get<std::string>();
  msg.old_tick_size = parse_double(j["old_tick_size"]);
  msg.new_tick_size = parse_double(j["new_tick_size"]);
  msg.timestamp = parse_timestamp(j["timestamp"]);
  return msg;
}

MarketResolvedMessage JsonParser::parse_market_resolved_message(const nlohmann::json& j) {
  MarketResolvedMessage msg;
  msg.market = j["market"].get<std::string>();
  msg.winning_asset_id = j["winning_asset_id"].get<std::string>();
  msg.winning_outcome = parse_outcome(j["winning_outcome"].get<std::string>());
  msg.timestamp = parse_timestamp(j["timestamp"]);

  if (j.contains("assets_ids") && j["assets_ids"].is_array()) {
    for (const auto& id : j["assets_ids"]) {
      msg.asset_ids.push_back(id.get<std::string>());
    }
  }

  if (j.contains("outcomes") && j["outcomes"].is_array()) {
    for (const auto& outcome : j["outcomes"]) {
      msg.outcomes.push_back(parse_outcome(outcome.get<std::string>()));
    }
  }

  return msg;
}

int JsonParser::parse_int(const nlohmann::json& j) {
  if (j.is_string()) {
    try {
      return std::stoi(j.get<std::string>());
    } catch (...) {
      LOG_ERROR("Failed to parse int from string: {}", j.get<std::string>());
      return 0;
    }
  } else if (j.is_number()) {
    return j.get<int>();
  }
  return 0;
}

double JsonParser::parse_double(const nlohmann::json& j) {
  if (j.is_string()) {
    try {
      return std::stod(j.get<std::string>());
    } catch (...) {
      LOG_ERROR("Failed to parse double from string: {}", j.get<std::string>());
      return 0.0;
    }
  } else if (j.is_number()) {
    return j.get<double>();
  }
  return 0.0;
}

uint64_t JsonParser::parse_timestamp(const nlohmann::json& j) {
  if (j.is_string()) {
    try {
      return std::stoull(j.get<std::string>());
    } catch (...) {
      LOG_ERROR("Failed to parse timestamp from string: {}", j.get<std::string>());
      return 0;
    }
  } else if (j.is_number()) {
    return j.get<uint64_t>();
  }
  return 0;
}

Side JsonParser::parse_side(const std::string& side_str) {
  if (side_str == "BUY" || side_str == "buy" || side_str == "Buy") {
    return Side::Buy;
  } else if (side_str == "SELL" || side_str == "sell" || side_str == "Sell") {
    return Side::Sell;
  } else {
    LOG_ERROR("Unknown side: {}", side_str);
    return Side::Sell;
  }
}

Outcome JsonParser::parse_outcome(const std::string& outcome_str) {
  if (to_upper(outcome_str) == "YES" || to_upper(outcome_str) == "UP") {
    return Outcome::Yes;
  } else if (to_upper(outcome_str) == "NO" || to_upper(outcome_str) == "DOWN") {
    return Outcome::No;
  } else {
    LOG_ERROR("Unknown outcome: {}", outcome_str);
    return Outcome::No;
  }
}
