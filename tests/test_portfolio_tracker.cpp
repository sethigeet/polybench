#include <gtest/gtest.h>

#include <cmath>

#include "portfolio_tracker.hpp"
#include "types/common.hpp"

class PortfolioTrackerTest : public ::testing::Test {
 protected:
  PortfolioTracker tracker;
  MarketId market_id = "test-market";
};

TEST_F(PortfolioTrackerTest, OpenLongPosition) {
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;

  tracker.on_fill(fill);

  auto& positions = tracker.get_positions();
  ASSERT_EQ(positions.size(), 1);

  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());
  EXPECT_EQ(it->second.quantity, 10.0);
  EXPECT_EQ(it->second.avg_entry_price, 0.50);
  EXPECT_EQ(it->second.cost_basis, 5.0);  // 10 * 0.50
}

TEST_F(PortfolioTrackerTest, AddToLongPosition) {
  // First fill
  FillReport fill1;
  fill1.market_id = market_id;
  fill1.outcome = Outcome::Yes;
  fill1.order_id = 1;
  fill1.filled_price = 0.50;
  fill1.filled_quantity = 10.0;
  fill1.timestamp = 1000;
  fill1.side = Side::Buy;
  tracker.on_fill(fill1);

  // Second fill at different price
  FillReport fill2;
  fill2.market_id = market_id;
  fill2.outcome = Outcome::Yes;
  fill2.order_id = 2;
  fill2.filled_price = 0.60;
  fill2.filled_quantity = 10.0;
  fill2.timestamp = 2000;
  fill2.side = Side::Buy;
  tracker.on_fill(fill2);

  auto& positions = tracker.get_positions();
  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());

  EXPECT_EQ(it->second.quantity, 20.0);
  EXPECT_DOUBLE_EQ(it->second.cost_basis, 11.0);  // 5.0 + 6.0
  EXPECT_DOUBLE_EQ(it->second.avg_entry_price, 0.55);  // VWAP: 11.0 / 20.0
}

TEST_F(PortfolioTrackerTest, CloseLongRealizePnL) {
  // Open long
  FillReport open_fill;
  open_fill.market_id = market_id;
  open_fill.outcome = Outcome::Yes;
  open_fill.order_id = 1;
  open_fill.filled_price = 0.50;
  open_fill.filled_quantity = 10.0;
  open_fill.timestamp = 1000;
  open_fill.side = Side::Buy;
  tracker.on_fill(open_fill);

  // Close at profit
  FillReport close_fill;
  close_fill.market_id = market_id;
  close_fill.outcome = Outcome::Yes;
  close_fill.order_id = 2;
  close_fill.filled_price = 0.60;
  close_fill.filled_quantity = 10.0;
  close_fill.timestamp = 2000;
  close_fill.side = Side::Sell;
  tracker.on_fill(close_fill);

  // PnL = 10 * (0.60 - 0.50) = 1.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 1.0);

  auto& positions = tracker.get_positions();
  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());
  EXPECT_EQ(it->second.quantity, 0.0);
}

TEST_F(PortfolioTrackerTest, OpenShortPosition) {
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Sell;

  tracker.on_fill(fill);

  auto& positions = tracker.get_positions();
  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());
  EXPECT_EQ(it->second.quantity, -10.0);
  EXPECT_EQ(it->second.avg_entry_price, 0.50);
}

TEST_F(PortfolioTrackerTest, CloseShortRealizePnL) {
  // Open short
  FillReport open_fill;
  open_fill.market_id = market_id;
  open_fill.outcome = Outcome::Yes;
  open_fill.order_id = 1;
  open_fill.filled_price = 0.60;
  open_fill.filled_quantity = 10.0;
  open_fill.timestamp = 1000;
  open_fill.side = Side::Sell;
  tracker.on_fill(open_fill);

  // Cover at profit (bought back cheaper)
  FillReport close_fill;
  close_fill.market_id = market_id;
  close_fill.outcome = Outcome::Yes;
  close_fill.order_id = 2;
  close_fill.filled_price = 0.50;
  close_fill.filled_quantity = 10.0;
  close_fill.timestamp = 2000;
  close_fill.side = Side::Buy;
  tracker.on_fill(close_fill);

  // PnL = 10 * (0.60 - 0.50) = 1.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 1.0);
}

TEST_F(PortfolioTrackerTest, FlipLongToShort) {
  // Open long
  FillReport long_fill;
  long_fill.market_id = market_id;
  long_fill.outcome = Outcome::Yes;
  long_fill.order_id = 1;
  long_fill.filled_price = 0.50;
  long_fill.filled_quantity = 10.0;
  long_fill.timestamp = 1000;
  long_fill.side = Side::Buy;
  tracker.on_fill(long_fill);

  // Sell more than position to flip to short
  FillReport flip_fill;
  flip_fill.market_id = market_id;
  flip_fill.outcome = Outcome::Yes;
  flip_fill.order_id = 2;
  flip_fill.filled_price = 0.60;
  flip_fill.filled_quantity = 15.0;  // 10 to close, 5 to open short
  flip_fill.timestamp = 2000;
  flip_fill.side = Side::Sell;
  tracker.on_fill(flip_fill);

  // Realized PnL from closing 10 @ (0.60 - 0.50) = 1.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 1.0);

  auto& positions = tracker.get_positions();
  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());
  EXPECT_EQ(it->second.quantity, -5.0);
  EXPECT_EQ(it->second.avg_entry_price, 0.60);  // New short entry
}

TEST_F(PortfolioTrackerTest, UnrealizedPnL) {
  // Open long
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  // Update mid price
  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.55);

  // Unrealized = 10 * (0.55 - 0.50) = 0.5
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.5);
}

TEST_F(PortfolioTrackerTest, UnrealizedPnLShort) {
  // Open short
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.60;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Sell;
  tracker.on_fill(fill);

  // Update mid price (moved in our favor)
  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.55);

  // Unrealized = 10 * (0.60 - 0.55) = 0.5 (with floating point tolerance)
  EXPECT_NEAR(tracker.get_unrealized_pnl(), 0.5, 1e-9);
}

TEST_F(PortfolioTrackerTest, TotalPnL) {
  // Open and close for realized PnL
  FillReport open_fill;
  open_fill.market_id = market_id;
  open_fill.outcome = Outcome::Yes;
  open_fill.order_id = 1;
  open_fill.filled_price = 0.50;
  open_fill.filled_quantity = 10.0;
  open_fill.timestamp = 1000;
  open_fill.side = Side::Buy;
  tracker.on_fill(open_fill);

  FillReport close_fill;
  close_fill.market_id = market_id;
  close_fill.outcome = Outcome::Yes;
  close_fill.order_id = 2;
  close_fill.filled_price = 0.55;
  close_fill.filled_quantity = 5.0;  // Partial close
  close_fill.timestamp = 2000;
  close_fill.side = Side::Sell;
  tracker.on_fill(close_fill);

  // Realized = 5 * (0.55 - 0.50) = 0.25
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 0.25);

  // Update mid price for remaining position
  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.60);

  // Unrealized = 5 * (0.60 - 0.50) = 0.5
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.5);

  // Total = 0.25 + 0.5 = 0.75
  EXPECT_DOUBLE_EQ(tracker.get_total_pnl(), 0.75);
}

TEST_F(PortfolioTrackerTest, SharpeRatioInsufficientData) {
  EXPECT_DOUBLE_EQ(tracker.get_sharpe_ratio(), 0.0);

  tracker.record_equity_snapshot();
  EXPECT_DOUBLE_EQ(tracker.get_sharpe_ratio(), 0.0);  // Still only 1 point
}

TEST_F(PortfolioTrackerTest, SharpeRatioCalculation) {
  // Simulate some equity history
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.50);
  tracker.record_equity_snapshot();  // PnL = 0

  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.55);
  tracker.record_equity_snapshot();  // PnL = 0.5

  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.60);
  tracker.record_equity_snapshot();  // PnL = 1.0

  double sharpe = tracker.get_sharpe_ratio();
  EXPECT_GT(sharpe, 0.0);  // Should be positive with consistent gains
}

TEST_F(PortfolioTrackerTest, SharpeRatioZeroVolatility) {
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  // Same PnL every snapshot = zero volatility
  tracker.update_mark_to_market(market_id, Outcome::Yes, 0.55);
  tracker.record_equity_snapshot();
  tracker.record_equity_snapshot();
  tracker.record_equity_snapshot();

  EXPECT_DOUBLE_EQ(tracker.get_sharpe_ratio(), 0.0);  // Returns are 0, stddev is 0
}

TEST_F(PortfolioTrackerTest, MarketResolvedLongYesWins) {
  // Open long YES position
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  // Market resolves YES wins (settles at 1.0)
  tracker.on_market_resolved(market_id, Outcome::Yes);

  // Realized PnL = 10 * (1.0 - 0.50) = 5.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 5.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);

  // Position should be cleared
  auto& positions = tracker.get_positions();
  auto it = positions.find({market_id, Outcome::Yes});
  ASSERT_NE(it, positions.end());
  EXPECT_EQ(it->second.quantity, 0.0);
}

TEST_F(PortfolioTrackerTest, MarketResolvedLongYesLoses) {
  // Open long YES position
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.50;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  // Market resolves NO wins (YES settles at 0.0)
  tracker.on_market_resolved(market_id, Outcome::No);

  // Realized PnL = 10 * (0.0 - 0.50) = -5.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), -5.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);
}

TEST_F(PortfolioTrackerTest, MarketResolvedLongNoWins) {
  // Open long NO position
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::No;
  fill.order_id = 1;
  fill.filled_price = 0.40;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Buy;
  tracker.on_fill(fill);

  // Market resolves NO wins (settles at 1.0)
  tracker.on_market_resolved(market_id, Outcome::No);

  // Realized PnL = 10 * (1.0 - 0.40) = 6.0
  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 6.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);
}

TEST_F(PortfolioTrackerTest, MarketResolvedShortYesWins) {
  // Open short YES position
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.60;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Sell;
  tracker.on_fill(fill);

  // Market resolves YES wins (settles at 1.0)
  // Short loses: (0.60 - 1.0) * 10 = -4.0
  tracker.on_market_resolved(market_id, Outcome::Yes);

  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), -4.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);
}

TEST_F(PortfolioTrackerTest, MarketResolvedShortYesLoses) {
  // Open short YES position
  FillReport fill;
  fill.market_id = market_id;
  fill.outcome = Outcome::Yes;
  fill.order_id = 1;
  fill.filled_price = 0.60;
  fill.filled_quantity = 10.0;
  fill.timestamp = 1000;
  fill.side = Side::Sell;
  tracker.on_fill(fill);

  // Market resolves NO wins (YES settles at 0.0)
  // Short wins: (0.60 - 0.0) * 10 = 6.0
  tracker.on_market_resolved(market_id, Outcome::No);

  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 6.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);
}

TEST_F(PortfolioTrackerTest, MarketResolvedBothSides) {
  // Open positions on both sides
  FillReport yes_fill;
  yes_fill.market_id = market_id;
  yes_fill.outcome = Outcome::Yes;
  yes_fill.order_id = 1;
  yes_fill.filled_price = 0.55;
  yes_fill.filled_quantity = 10.0;
  yes_fill.timestamp = 1000;
  yes_fill.side = Side::Buy;
  tracker.on_fill(yes_fill);

  FillReport no_fill;
  no_fill.market_id = market_id;
  no_fill.outcome = Outcome::No;
  no_fill.order_id = 2;
  no_fill.filled_price = 0.45;
  no_fill.filled_quantity = 10.0;
  no_fill.timestamp = 1000;
  no_fill.side = Side::Buy;
  tracker.on_fill(no_fill);

  // Market resolves YES wins
  // YES: (1.0 - 0.55) * 10 = 4.5
  // NO: (0.0 - 0.45) * 10 = -4.5
  // Total should be 0
  tracker.on_market_resolved(market_id, Outcome::Yes);

  EXPECT_DOUBLE_EQ(tracker.get_realized_pnl(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.get_unrealized_pnl(), 0.0);
}
