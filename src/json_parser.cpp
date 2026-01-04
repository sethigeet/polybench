#include "json_parser.hpp"

#include <charconv>
#include <cstring>

#define LOGGER_NAME "JsonParser"
#include "logger.hpp"

std::vector<PolymarketMessage> JsonParser::parse(const std::string& json_str) {
  nlohmann::json j;
  // Use accept() for validation + parse, avoiding exception on malformed JSON
  if (!nlohmann::json::accept(json_str)) {
    LOG_ERROR("Failed to parse message: invalid JSON");
    return {};
  }
  j = nlohmann::json::parse(json_str, nullptr, false, true);

  if (j.is_array()) {
    std::vector<PolymarketMessage> messages;
    messages.reserve(j.size());
    for (const auto& item : j) {
      if (auto msg = parse_object(item); msg.has_value()) {
        messages.push_back(std::move(*msg));
      }
    }
    return messages;
  }

  if (auto msg = parse_object(j); msg.has_value()) {
    return {std::move(*msg)};
  }
  return {};
}

std::optional<PolymarketMessage> JsonParser::parse_object(const nlohmann::json& j) {
  auto it = j.find("event_type");
  if (it == j.end() || !it->is_string()) {
    LOG_WARN("No event type found");
    return std::nullopt;
  }

  const auto& event_type = it->get_ref<const std::string&>();

  if (event_type == "book") {
    return parse_book_message(j);
  } else if (event_type == "price_change") {
    return parse_price_change_message(j);
  } else if (event_type == "last_trade_price") {
    return parse_last_trade_message(j);
  } else if (event_type == "tick_size_change") {
    return parse_tick_size_change_message(j);
  } else if (event_type == "market_resolved") {
    return parse_market_resolved_message(j);
  } else if (event_type == "market_created" || event_type == "best_bid_ask") {
    return std::nullopt;
  } else {
    LOG_WARN("Unknown event type: {}", event_type);
    return std::nullopt;
  }
}

BookMessage JsonParser::parse_book_message(const nlohmann::json& j) {
  BookMessage msg;
  msg.asset_id = j["asset_id"].get<std::string>();
  msg.market = j["market"].get<std::string>();
  msg.timestamp = parse_timestamp(j["timestamp"]);

  if (j.contains("bids") && j["bids"].is_array()) {
    for (const auto& bid : j["bids"]) {
      OrderSummary order;
      order.price = parse_double(bid["price"]);
      order.size = parse_double(bid["size"]);
      msg.bids.push_back(order);
    }
  }

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

  auto price_changes = j.find("price_changes");
  if (price_changes != j.end() && price_changes->is_array()) {
    for (const auto& pc : *price_changes) {
      PriceChange change;
      change.asset_id = pc["asset_id"].get<std::string>();
      change.price = parse_double(pc["price"]);
      change.size = parse_double(pc["size"]);
      change.side = parse_side(pc["side"]);
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
  msg.side = parse_side(j["side"]);
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
  msg.winning_outcome = parse_outcome(j["winning_outcome"]);
  msg.timestamp = parse_timestamp(j["timestamp"]);

  if (j.contains("assets_ids") && j["assets_ids"].is_array()) {
    for (const auto& id : j["assets_ids"]) {
      msg.asset_ids.push_back(id.get<std::string>());
    }
  }

  if (j.contains("outcomes") && j["outcomes"].is_array()) {
    for (const auto& outcome : j["outcomes"]) {
      msg.outcomes.push_back(parse_outcome(outcome));
    }
  }

  return msg;
}

int JsonParser::parse_int(const nlohmann::json& j) {
  if (j.is_string()) {
    const auto& str = j.get_ref<const std::string&>();
    int value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse int from string: {}", str);
      return 0;
    }
    return value;
  } else if (j.is_number()) {
    return j.get<int>();
  }
  return 0;
}

double JsonParser::parse_double(const nlohmann::json& j) {
  if (j.is_string()) {
    const auto& str = j.get_ref<const std::string&>();
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse double from string: {}", str);
      return 0.0;
    }
    return value;
  } else if (j.is_number()) {
    return j.get<double>();
  }
  return 0.0;
}

uint64_t JsonParser::parse_timestamp(const nlohmann::json& j) {
  if (j.is_string()) {
    const auto& str = j.get_ref<const std::string&>();
    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse timestamp from string: {}", str);
      return 0;
    }
    return value;
  } else if (j.is_number()) {
    return j.get<uint64_t>();
  }
  return 0;
}

Side JsonParser::parse_side(const nlohmann::json& j) {
  const auto& side_str = j.get_ref<const std::string&>();
  if (!side_str.empty()) {
    char first = side_str[0] | 0x20;  // lowercase via bitmask
    if (first == 'b') return Side::Buy;
    if (first == 's') return Side::Sell;
  }
  LOG_ERROR("Unknown side: {}", side_str);
  return Side::Sell;
}

Outcome JsonParser::parse_outcome(const nlohmann::json& j) {
  const auto& outcome_str = j.get_ref<const std::string&>();
  if (!outcome_str.empty()) {
    char first = outcome_str[0] | 0x20;  // lowercase via bitmask
    if (first == 'y' || first == 'u') return Outcome::Yes;
    if (first == 'n' || first == 'd') return Outcome::No;
  }
  LOG_ERROR("Unknown outcome: {}", outcome_str);
  return Outcome::No;
}
