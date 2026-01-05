#pragma once

#include <simdjson.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "types/polymarket.hpp"

using PolymarketMessage = std::variant<BookMessage, PriceChangeMessage, LastTradeMessage,
                                       TickSizeChangeMessage, MarketResolvedMessage>;

class JsonParser {
 public:
  JsonParser();

  std::vector<PolymarketMessage> parse(const std::string& json_str);

 private:
  simdjson::ondemand::parser parser_;
  simdjson::padded_string padded_buffer_;

  std::optional<PolymarketMessage> parse_object(simdjson::ondemand::object& obj);

  BookMessage parse_book_message(simdjson::ondemand::object& obj);
  PriceChangeMessage parse_price_change_message(simdjson::ondemand::object& obj);
  LastTradeMessage parse_last_trade_message(simdjson::ondemand::object& obj);
  TickSizeChangeMessage parse_tick_size_change_message(simdjson::ondemand::object& obj);
  MarketResolvedMessage parse_market_resolved_message(simdjson::ondemand::object& obj);

  static double parse_double(simdjson::ondemand::value val);
  static int parse_int(simdjson::ondemand::value val);
  static uint64_t parse_timestamp(simdjson::ondemand::value val);
  static Side parse_side(simdjson::ondemand::value val) noexcept;
  static Outcome parse_outcome(simdjson::ondemand::value val) noexcept;
};
