#pragma once
#include <unordered_map>
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
  // Maximum equity history samples before using circular buffer (~27 hours at 1 sample/sec)
  static constexpr size_t kMaxEquityHistory = 100000;

  using PositionKey = std::pair<MarketId, Outcome>;

  struct PositionKeyHash {
    size_t operator()(const PositionKey& key) const noexcept {
      size_t h1 = std::hash<MarketId>{}(key.first);
      size_t h2 = static_cast<size_t>(key.second);
      return h1 ^ (h2 << 1);
    }
  };

  using PositionMap = std::unordered_map<PositionKey, Position, PositionKeyHash>;

  PortfolioTracker();

  void on_fill(const FillReport& fill);
  void on_market_resolved(const MarketId& market_id, Outcome winning_outcome);
  void update_mark_to_market(const MarketId& market_id, Outcome outcome, double mid_price);
  void record_equity_snapshot();

  double get_realized_pnl() const noexcept { return realized_pnl_; }
  double get_unrealized_pnl() const;
  double get_total_pnl() const { return get_realized_pnl() + get_unrealized_pnl(); }
  double get_sharpe_ratio() const;

  const PositionMap& get_positions() const { return positions_; }

 private:
  PositionMap positions_;
  std::unordered_map<PositionKey, double, PositionKeyHash> mid_prices_;
  double realized_pnl_ = 0.0;
  std::vector<double> equity_history_;
  size_t equity_write_idx_ = 0;

  double returns_mean_ = 0.0;
  double returns_m2_ = 0.0;  // Sum of squared deviations
  size_t returns_count_ = 0;
  double prev_equity_ = 0.0;
};
