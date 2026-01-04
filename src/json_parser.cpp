#include "json_parser.hpp"

#include <charconv>
#include <cstring>

#define LOGGER_NAME "JsonParser"
#include "logger.hpp"

JsonParser::JsonParser() : padded_buffer_(1024) {}

std::vector<PolymarketMessage> JsonParser::parse(const std::string& json_str) {
  // Resize padded buffer if needed
  if (padded_buffer_.size() < json_str.size()) {
    padded_buffer_ = simdjson::padded_string(json_str.size() * 2);
  }

  std::memcpy(padded_buffer_.data(), json_str.data(), json_str.size());

  auto doc_result = parser_.iterate(padded_buffer_.data(), json_str.size(), padded_buffer_.size());
  if (doc_result.error()) {
    LOG_ERROR("Failed to parse message: {}", simdjson::error_message(doc_result.error()));
    return {};
  }

  auto doc = std::move(doc_result).value();
  std::vector<PolymarketMessage> messages;

  auto type_result = doc.type().value();
  if (type_result == simdjson::ondemand::json_type::array) {
    auto arr_result = doc.get_array().value();
    for (auto element : arr_result) {
      auto obj_result = element.get_object().value();
      if (auto msg = parse_object(obj_result); msg.has_value()) {
        messages.push_back(std::move(*msg));
      }
    }
  } else {
    auto obj_result = doc.get_object().value();
    if (auto msg = parse_object(obj_result); msg.has_value()) {
      messages.push_back(std::move(*msg));
    }
  }

  return messages;
}

std::optional<PolymarketMessage> JsonParser::parse_object(simdjson::ondemand::object obj) {
  auto event_type_result = obj["event_type"].get_string();
  if (event_type_result.error()) {
    LOG_WARN("No event type found");
    return std::nullopt;
  }

  std::string_view event_type = event_type_result.value();

  // Reset object iterator to parse from beginning
  obj.reset();

  if (event_type == "book") {
    return parse_book_message(obj);
  } else if (event_type == "price_change") {
    return parse_price_change_message(obj);
  } else if (event_type == "last_trade_price") {
    return parse_last_trade_message(obj);
  } else if (event_type == "tick_size_change") {
    return parse_tick_size_change_message(obj);
  } else if (event_type == "market_resolved") {
    return parse_market_resolved_message(obj);
  } else if (event_type == "new_market" || event_type == "best_bid_ask") {
    return std::nullopt;
  } else {
    LOG_WARN("Unknown event type: {}", event_type);
    return std::nullopt;
  }
}

BookMessage JsonParser::parse_book_message(simdjson::ondemand::object obj) {
  BookMessage msg;
  msg.asset_id = obj["asset_id"].value().get_string().value();
  msg.market = obj["market"].value().get_string().value();
  msg.timestamp = parse_timestamp(obj["timestamp"].value());

  auto bids_arr = obj["bids"].value().get_array().value();
  for (auto bid : bids_arr) {
    OrderSummary order;
    auto bid_obj = bid.get_object().value();
    order.price = parse_double(bid_obj["price"].value());
    order.size = parse_double(bid_obj["size"].value());
    msg.bids.push_back(order);
  }

  auto asks_arr = obj["asks"].value().get_array().value();
  for (auto ask : asks_arr) {
    OrderSummary order;
    auto ask_obj = ask.get_object().value();
    order.price = parse_double(ask_obj["price"].value());
    order.size = parse_double(ask_obj["size"].value());
    msg.asks.push_back(order);
  }

  return msg;
}

PriceChangeMessage JsonParser::parse_price_change_message(simdjson::ondemand::object obj) {
  PriceChangeMessage msg;
  msg.market = obj["market"].value().get_string().value();
  msg.timestamp = parse_timestamp(obj["timestamp"].value());
  auto changes_arr = obj["price_changes"].value().get_array().value();
  for (auto change_elem : changes_arr) {
    PriceChange change;
    auto change_obj = change_elem.get_object().value();
    change.asset_id = change_obj["asset_id"].value().get_string().value();
    change.price = parse_double(change_obj["price"].value());
    change.size = parse_double(change_obj["size"].value());
    change.side = parse_side(change_obj["side"].value().get_string().value());
    change.best_bid = parse_double(change_obj["best_bid"].value());
    change.best_ask = parse_double(change_obj["best_ask"].value());
    msg.price_changes.push_back(change);
  }

  return msg;
}

LastTradeMessage JsonParser::parse_last_trade_message(simdjson::ondemand::object obj) {
  LastTradeMessage msg;
  msg.asset_id = obj["asset_id"].value().get_string().value();
  msg.market = obj["market"].value().get_string().value();
  msg.price = parse_double(obj["price"].value());
  msg.side = parse_side(obj["side"].value().get_string().value());
  msg.size = parse_double(obj["size"].value());
  msg.fee_rate_bps = parse_int(obj["fee_rate_bps"].value());
  msg.timestamp = parse_timestamp(obj["timestamp"].value());

  return msg;
}

TickSizeChangeMessage JsonParser::parse_tick_size_change_message(simdjson::ondemand::object obj) {
  TickSizeChangeMessage msg;
  msg.asset_id = obj["asset_id"].value().get_string().value();
  msg.market = obj["market"].value().get_string().value();
  msg.old_tick_size = parse_double(obj["old_tick_size"].value());
  msg.new_tick_size = parse_double(obj["new_tick_size"].value());
  msg.timestamp = parse_timestamp(obj["timestamp"].value());

  return msg;
}

MarketResolvedMessage JsonParser::parse_market_resolved_message(simdjson::ondemand::object obj) {
  MarketResolvedMessage msg;
  msg.market = obj["market"].value().get_string().value();
  msg.winning_asset_id = obj["winning_asset_id"].value().get_string().value();
  msg.winning_outcome = parse_outcome(obj["winning_outcome"].value().get_string().value());
  msg.timestamp = parse_timestamp(obj["timestamp"].value());
  auto ids_arr = obj["assets_ids"].value().get_array().value();
  for (auto id : ids_arr) {
    msg.asset_ids.push_back(std::string(id.get_string().value()));
  }
  auto outcomes_arr = obj["outcomes"].value().get_array().value();
  for (auto outcome : outcomes_arr) {
    msg.outcomes.push_back(parse_outcome(outcome.get_string().value()));
  }

  return msg;
}

double JsonParser::parse_double(simdjson::ondemand::value val) {
  auto type_result = val.type();
  if (type_result.error()) return 0.0;

  if (type_result.value() == simdjson::ondemand::json_type::string) {
    auto str_result = val.get_string();
    if (str_result.error()) return 0.0;
    std::string_view str = str_result.value();
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse double from string: {}", str);
      return 0.0;
    }
    return value;
  } else if (type_result.value() == simdjson::ondemand::json_type::number) {
    auto num_result = val.get_double();
    if (num_result.error()) return 0.0;
    return num_result.value();
  }
  return 0.0;
}

int JsonParser::parse_int(simdjson::ondemand::value val) {
  auto type_result = val.type();
  if (type_result.error()) return 0;

  if (type_result.value() == simdjson::ondemand::json_type::string) {
    auto str_result = val.get_string();
    if (str_result.error()) return 0;
    std::string_view str = str_result.value();
    int value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse int from string: {}", str);
      return 0;
    }
    return value;
  } else if (type_result.value() == simdjson::ondemand::json_type::number) {
    auto num_result = val.get_int64();
    if (num_result.error()) return 0;
    return static_cast<int>(num_result.value());
  }
  return 0;
}

uint64_t JsonParser::parse_timestamp(simdjson::ondemand::value val) {
  auto type_result = val.type();
  if (type_result.error()) return 0;

  if (type_result.value() == simdjson::ondemand::json_type::string) {
    auto str_result = val.get_string();
    if (str_result.error()) return 0;
    std::string_view str = str_result.value();
    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc{}) {
      LOG_ERROR("Failed to parse timestamp from string: {}", str);
      return 0;
    }
    return value;
  } else if (type_result.value() == simdjson::ondemand::json_type::number) {
    auto num_result = val.get_uint64();
    if (num_result.error()) return 0;
    return num_result.value();
  }
  return 0;
}

Side JsonParser::parse_side(std::string_view str) {
  if (!str.empty()) {
    char first = str[0] | 0x20;  // lowercase via bitmask
    if (first == 'b') return Side::Buy;
    if (first == 's') return Side::Sell;
  }
  LOG_ERROR("Unknown side: {}", str);
  return Side::Sell;
}

Outcome JsonParser::parse_outcome(std::string_view str) {
  if (!str.empty()) {
    char first = str[0] | 0x20;  // lowercase via bitmask
    if (first == 'y' || first == 'u') return Outcome::Yes;
    if (first == 'n' || first == 'd') return Outcome::No;
  }
  LOG_ERROR("Unknown outcome: {}", str);
  return Outcome::No;
}
