#pragma once

#include <simdjson.h>

#include <optional>
#include <string_view>

#include "perf_stats.hpp"
#include "types/polymarket.hpp"
#include "types/small_vector.hpp"

class JsonParser {
 public:
  JsonParser();

  template <size_t N>
  size_t parse(std::string_view json_str, SmallVector<PolymarketMessage, N>& out,
               PerfStats* perf_stats = nullptr);

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
  static std::string_view message_type_name(const PolymarketMessage& msg) noexcept;
};
