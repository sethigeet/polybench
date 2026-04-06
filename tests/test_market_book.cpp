#include <gtest/gtest.h>

#include "trading/market_book.hpp"
#include "types/common.hpp"
#include "types/polymarket.hpp"

class MarketBookTest : public ::testing::Test {
 protected:
  MarketBook book;
  AssetId yes_asset = "yes-asset-id";
  AssetId no_asset = "no-asset-id";

  void SetUp() override {
    book.register_asset(yes_asset, Outcome::Yes);
    book.register_asset(no_asset, Outcome::No);
  }
};

TEST_F(MarketBookTest, RegisterAssetAndGetOutcome) {
  auto outcome = book.get_outcome(yes_asset);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(*outcome, Outcome::Yes);

  outcome = book.get_outcome(no_asset);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(*outcome, Outcome::No);
}

TEST_F(MarketBookTest, GetOutcomeUnknownAsset) {
  auto outcome = book.get_outcome(AssetId("unknown"));
  EXPECT_FALSE(outcome.has_value());
}

TEST_F(MarketBookTest, OnBookMessagePopulatesYesBidsAsks) {
  BookMessage msg;
  msg.asset_id = yes_asset;
  msg.market = MarketId("market-1");
  msg.timestamp = 1000;
  msg.bids.push_back({0.50, 100.0});
  msg.bids.push_back({0.49, 200.0});
  msg.asks.push_back({0.51, 150.0});
  msg.asks.push_back({0.52, 250.0});

  book.on_book_message(msg);

  EXPECT_EQ(book.get_yes_best_bid(), 0.50);
  EXPECT_EQ(book.get_yes_best_ask(), 0.51);
  EXPECT_EQ(book.get_yes_bid_depth(0.50), 100.0);
  EXPECT_EQ(book.get_yes_bid_depth(0.49), 200.0);
  EXPECT_EQ(book.get_yes_ask_depth(0.51), 150.0);
}

TEST_F(MarketBookTest, OnBookMessagePopulatesNoBidsAsks) {
  BookMessage msg;
  msg.asset_id = no_asset;
  msg.market = MarketId("market-1");
  msg.timestamp = 1000;
  msg.bids.push_back({0.40, 50.0});
  msg.asks.push_back({0.42, 75.0});

  book.on_book_message(msg);

  EXPECT_EQ(book.get_no_best_bid(), 0.40);
  EXPECT_EQ(book.get_no_best_ask(), 0.42);
}

TEST_F(MarketBookTest, OnPriceChangeUpdatesBid) {
  // Initialize with a book message first
  BookMessage init_msg;
  init_msg.asset_id = yes_asset;
  init_msg.market = MarketId("market-1");
  init_msg.timestamp = 1000;
  init_msg.bids.push_back({0.50, 100.0});
  book.on_book_message(init_msg);

  // Update the price level
  PriceChange change;
  change.asset_id = yes_asset;
  change.price = 0.50;
  change.size = 150.0;
  change.side = Side::Buy;
  change.best_bid = 0.50;
  change.best_ask = 0.51;

  book.on_price_change(change);

  EXPECT_EQ(book.get_yes_bid_depth(0.50), 150.0);
}

TEST_F(MarketBookTest, OnPriceChangeRemovesLevel) {
  BookMessage init_msg;
  init_msg.asset_id = yes_asset;
  init_msg.market = MarketId("market-1");
  init_msg.timestamp = 1000;
  init_msg.bids.push_back({0.50, 100.0});
  book.on_book_message(init_msg);

  // Remove the level by setting size to 0
  PriceChange change;
  change.asset_id = yes_asset;
  change.price = 0.50;
  change.size = 0.0;
  change.side = Side::Buy;
  change.best_bid = 0.49;
  change.best_ask = 0.51;

  book.on_price_change(change);

  EXPECT_EQ(book.get_yes_bid_depth(0.50), 0.0);
  EXPECT_FALSE(book.get_yes_best_bid().has_value());
}

TEST_F(MarketBookTest, BestBidAskEmpty) {
  EXPECT_FALSE(book.get_yes_best_bid().has_value());
  EXPECT_FALSE(book.get_yes_best_ask().has_value());
  EXPECT_FALSE(book.get_no_best_bid().has_value());
  EXPECT_FALSE(book.get_no_best_ask().has_value());
}

TEST_F(MarketBookTest, VirtualOrderAddAndRemove) {
  MarketId mid("market-1");
  VirtualOrder order;
  order.market_id = mid;
  order.outcome = Outcome::Yes;
  order.id = 123;
  order.price = 0.50;
  order.quantity = 10.0;
  order.side = Side::Buy;
  order.volume_ahead = 50.0;
  order.placed_at = 1000;

  book.add_virtual_order(order);

  auto& orders = book.get_virtual_orders(mid);
  ASSERT_EQ(orders.size(), 1);
  EXPECT_EQ(orders[0].id, 123);
  EXPECT_EQ(orders[0].price, 0.50);

  book.remove_virtual_order(mid, 123);
  EXPECT_TRUE(book.get_virtual_orders(mid).empty());
}

TEST_F(MarketBookTest, RemoveNonExistentVirtualOrder) {
  MarketId mid("market-1");
  // Should not throw or crash
  book.remove_virtual_order(mid, 999);
  EXPECT_TRUE(book.get_virtual_orders(mid).empty());
}
