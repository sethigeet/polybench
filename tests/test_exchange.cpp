#include <gtest/gtest.h>

#include <unordered_map>

#include "exchange.hpp"
#include "market_book.hpp"
#include "types/common.hpp"

class ExchangeTest : public ::testing::Test {
 protected:
  Exchange exchange;
  std::unordered_map<MarketId, MarketBook> books;
  MarketId market_id = "test-market";
  AssetId yes_asset = "yes-asset";
  AssetId no_asset = "no-asset";

  void SetUp() override {
    auto& book = books[market_id];
    book.register_asset(yes_asset, Outcome::Yes);
    book.register_asset(no_asset, Outcome::No);
    exchange.set_books(&books);
  }

  void setup_book_with_liquidity() {
    auto& book = books[market_id];
    BookMessage msg;
    msg.asset_id = yes_asset;
    msg.market = market_id;
    msg.timestamp = 1000;
    msg.bids.push_back({0.53, 100.0});
    msg.bids.push_back({0.50, 100.0});
    msg.bids.push_back({0.49, 200.0});
    msg.asks.push_back({0.55, 100.0});
    msg.asks.push_back({0.56, 200.0});
    book.on_book_message(msg);

    BookMessage no_msg;
    no_msg.asset_id = no_asset;
    no_msg.market = market_id;
    no_msg.timestamp = 1000;
    no_msg.bids.push_back({0.40, 100.0});
    no_msg.asks.push_back({0.50, 100.0});
    book.on_book_message(no_msg);
  }
};

TEST_F(ExchangeTest, PriceValidationBelowMin) {
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 1;
  order.price = 0.005;  // Below MIN_PRICE (0.01)
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  EXPECT_THROW(exchange.submit_order(order), PriceValidationError);
}

TEST_F(ExchangeTest, PriceValidationAboveMax) {
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 1;
  order.price = 0.995;  // Above MAX_PRICE (0.99)
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  EXPECT_THROW(exchange.submit_order(order), PriceValidationError);
}

TEST_F(ExchangeTest, TakerFillBuyYesMatchesSellYes) {
  setup_book_with_liquidity();

  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 1;
  order.price = 0.55;  // Crosses the ask at 0.55
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  auto fill = exchange.submit_order(order);
  ASSERT_TRUE(fill.has_value());
  EXPECT_EQ(fill->order_id, 1);
  EXPECT_EQ(fill->filled_price, 0.55);
  EXPECT_EQ(fill->filled_quantity, 10.0);
  EXPECT_EQ(fill->outcome, Outcome::Yes);
  EXPECT_EQ(fill->side, Side::Buy);
}

TEST_F(ExchangeTest, TakerFillBuyYesMatchesBetterSellNo) {
  setup_book_with_liquidity();

  // BUY YES @ 0.60 should also match SELL YES @ 0.55 (since that is a better price)
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 2;
  order.price = 0.60;
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  auto fill = exchange.submit_order(order);
  ASSERT_TRUE(fill.has_value());
  EXPECT_EQ(fill->filled_price, 0.55);
}

TEST_F(ExchangeTest, TakerFillBuyYesMatchesBuyNo) {
  setup_book_with_liquidity();

  // BUY NO @ 0.48 should also match BUY YES @ 0.52 (complement = 1 - 0.48 = 0.52)
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::No;
  order.id = 2;
  order.price = 0.47;
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  auto fill = exchange.submit_order(order);
  ASSERT_TRUE(fill.has_value());
  EXPECT_EQ(fill->filled_price, 0.47);
}

TEST_F(ExchangeTest, MakerOrderQueued) {
  setup_book_with_liquidity();

  // Order that won't cross spread
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 3;
  order.price = 0.51;  // Below ask, won't fill
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  auto fill = exchange.submit_order(order);
  EXPECT_FALSE(fill.has_value());

  // Check virtual order was added
  auto& book = books[market_id];
  auto& virtual_orders = book.get_virtual_orders();
  ASSERT_EQ(virtual_orders.size(), 1);
  EXPECT_EQ(virtual_orders[0].id, 3);
  EXPECT_EQ(virtual_orders[0].price, 0.51);
}

TEST_F(ExchangeTest, CancelOrder) {
  setup_book_with_liquidity();

  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 4;
  order.price = 0.51;
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  exchange.submit_order(order);

  auto& book = books[market_id];
  ASSERT_EQ(book.get_virtual_orders().size(), 1);

  exchange.cancel_order(market_id, 4);
  EXPECT_TRUE(book.get_virtual_orders().empty());
}

TEST_F(ExchangeTest, ProcessTradeFillsVirtualOrder) {
  setup_book_with_liquidity();

  // Add a maker SELL order at bid price (will be queued as maker)
  Order order;
  order.market_id = market_id;
  order.outcome = Outcome::Yes;
  order.id = 5;
  order.price = 0.54;  // Between best bid (0.53) and best ask (0.55) - won't cross
  order.quantity = 10.0;
  order.side = Side::Sell;
  order.timestamp = 1000;

  exchange.submit_order(order);

  auto& book = books[market_id];
  ASSERT_EQ(book.get_virtual_orders().size(), 1);
  // Volume ahead = YES bid depth at 0.54 (0) + NO ask depth at complement 0.46 (0) = 0
  // Since there's no liquidity at these prices, volume_ahead is 0
  double volume_ahead = book.get_virtual_orders()[0].volume_ahead;

  // Simulate a trade at the same price that would match our order
  LastTradeMessage trade;
  trade.asset_id = yes_asset;
  trade.market = market_id;
  trade.price = 0.54;
  trade.side = Side::Buy;  // Taker buys (we're selling)
  trade.size = 10.0;
  trade.fee_rate_bps = 0;
  trade.timestamp = 2000;

  auto fills = exchange.process_trade(trade);

  // Virtual order should be filled
  ASSERT_EQ(fills.size(), 1);
  EXPECT_EQ(fills[0].order_id, 5);
  EXPECT_EQ(fills[0].filled_price, 0.54);
}

TEST_F(ExchangeTest, SubmitOrderNoBook) {
  std::unordered_map<MarketId, MarketBook> empty_books;
  exchange.set_books(&empty_books);

  Order order;
  order.market_id = MarketId("nonexistent-market");
  order.outcome = Outcome::Yes;
  order.id = 1;
  order.price = 0.50;
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.timestamp = 1000;

  auto fill = exchange.submit_order(order);
  EXPECT_FALSE(fill.has_value());
}

TEST_F(ExchangeTest, ComplementPriceFunction) {
  EXPECT_DOUBLE_EQ(polymarket::complement_price(0.30), 0.70);
  EXPECT_DOUBLE_EQ(polymarket::complement_price(0.55), 0.45);
  EXPECT_DOUBLE_EQ(polymarket::complement_price(0.01), 0.99);
}

TEST_F(ExchangeTest, IsValidPriceFunction) {
  EXPECT_TRUE(polymarket::is_valid_price(0.01));
  EXPECT_TRUE(polymarket::is_valid_price(0.50));
  EXPECT_TRUE(polymarket::is_valid_price(0.99));
  EXPECT_FALSE(polymarket::is_valid_price(0.005));
  EXPECT_FALSE(polymarket::is_valid_price(0.995));
  EXPECT_FALSE(polymarket::is_valid_price(0.0));
  EXPECT_FALSE(polymarket::is_valid_price(1.0));
}
