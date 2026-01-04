#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "types/polymarket.hpp"

using PolymarketMessage = std::variant<BookMessage, PriceChangeMessage, LastTradeMessage,
                                       TickSizeChangeMessage, MarketResolvedMessage>;

class JsonParser {
 public:
  static std::vector<PolymarketMessage> parse(const std::string& json_str);

  static BookMessage parse_book_message(const nlohmann::json& j);
  static PriceChangeMessage parse_price_change_message(const nlohmann::json& j);
  static LastTradeMessage parse_last_trade_message(const nlohmann::json& j);
  static TickSizeChangeMessage parse_tick_size_change_message(const nlohmann::json& j);
  static MarketResolvedMessage parse_market_resolved_message(const nlohmann::json& j);

 private:
  static std::optional<PolymarketMessage> parse_object(const nlohmann::json& j);
  static int parse_int(const nlohmann::json& j);
  static double parse_double(const nlohmann::json& j);
  static uint64_t parse_timestamp(const nlohmann::json& j);
  static Side parse_side(const nlohmann::json& j);
  static Outcome parse_outcome(const nlohmann::json& j);
};
