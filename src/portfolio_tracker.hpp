#pragma once
#include <map>
#include <utility>
#include <vector>

#include "types/common.hpp"

struct Position {
  double quantity = 0.0;         // Net position (positive = long, negative = short)
  double avg_entry_price = 0.0;  // VWAP of entries
  double cost_basis = 0.0;       // Total cost spent on position
};

class PortfolioTracker {
 public:
  void on_fill(const FillReport& fill);
  void on_market_resolved(const MarketId& market_id, Outcome winning_outcome);
  void update_mark_to_market(const MarketId& market_id, Outcome outcome, double mid_price);
  void record_equity_snapshot();

  double get_realized_pnl() const noexcept { return realized_pnl_; }
  double get_unrealized_pnl() const;
  double get_total_pnl() const { return get_realized_pnl() + get_unrealized_pnl(); }
  double get_sharpe_ratio() const;

  const std::map<std::pair<MarketId, Outcome>, Position>& get_positions() const {
    return positions_;
  }

 private:
  using PositionKey = std::pair<MarketId, Outcome>;

  std::map<PositionKey, Position> positions_;
  std::map<PositionKey, double> mid_prices_;
  double realized_pnl_ = 0.0;
  std::vector<double> equity_history_;
};
