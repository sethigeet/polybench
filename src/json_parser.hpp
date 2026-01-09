#pragma once

#include <simdjson.h>

#include <optional>
#include <string>
#include <variant>

#include "types/polymarket.hpp"
#include "types/small_vector.hpp"

using PolymarketMessage = std::variant<BookMessage, PriceChangeMessage, LastTradeMessage,
                                       TickSizeChangeMessage, MarketResolvedMessage>;

class JsonParser {
 public:
  JsonParser();

  size_t parse(const std::string& json_str, SmallVector<PolymarketMessage, 2>& out);

 private:
  simdjson::ondemand::parser parser_;
  simdjson::padded_string padded_buffer_;

  std::optional<PolymarketMessage> parse_object(simdjson::ondemand::object& obj);

  BookMessage parse_book_message(simdjson::ondemand::object& obj);
  PriceChangeMessage parse_price_change_message(simdjson::ondemand::object& obj);
  LastTradeMessage parse_last_trade_message(simdjson::ondemand::object& obj);
  TickSizeChangeMessage parse_tick_size_change_message(simdjson::ondemand::object& obj);
  MarketResolvedMessage parse_market_resolved_message(simdjson::ondemand::object& obj);

  static double parse_double(std::string_view str);
  static int parse_int(std::string_view str);
  static uint64_t parse_timestamp(std::string_view str);
  static Side parse_side(std::string_view str) noexcept;
  static Outcome parse_outcome(std::string_view str) noexcept;
};
