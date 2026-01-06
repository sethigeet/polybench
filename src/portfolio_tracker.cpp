#include "portfolio_tracker.hpp"

#include <cmath>
#include <numeric>

void PortfolioTracker::on_fill(const FillReport& fill) {
  PositionKey key{fill.market_id, fill.outcome};
  Position& pos = positions_[key];

  double fill_qty = fill.filled_quantity;
  double fill_price = fill.filled_price;

  double signed_qty = (fill.side == Side::Buy) ? fill_qty : -fill_qty;

  double old_qty = pos.quantity;
  double new_qty = old_qty + signed_qty;

  if (fill.side == Side::Buy) {
    if (old_qty >= 0) {
      // Adding to long position: update VWAP
      double total_cost = pos.cost_basis + (fill_qty * fill_price);
      double total_qty = old_qty + fill_qty;
      pos.avg_entry_price = total_qty > 0 ? total_cost / total_qty : 0.0;
      pos.cost_basis = total_cost;
    } else {
      // Covering short position: realize PnL
      double cover_qty = std::min(fill_qty, -old_qty);
      realized_pnl_ += cover_qty * (pos.avg_entry_price - fill_price);

      // If we flip to long
      double remaining_buy = fill_qty - cover_qty;
      if (remaining_buy > 0) {
        pos.avg_entry_price = fill_price;
        pos.cost_basis = remaining_buy * fill_price;
      } else if (new_qty == 0) {
        pos.avg_entry_price = 0.0;
        pos.cost_basis = 0.0;
      }
    }
  } else {
    // Sell
    if (old_qty <= 0) {
      // Adding to short position: update VWAP
      double total_proceeds = pos.cost_basis + (fill_qty * fill_price);
      double total_qty = -old_qty + fill_qty;
      pos.avg_entry_price = total_qty > 0 ? total_proceeds / total_qty : 0.0;
      pos.cost_basis = total_proceeds;
    } else {
      // Closing long position: realize PnL
      double close_qty = std::min(fill_qty, old_qty);
      realized_pnl_ += close_qty * (fill_price - pos.avg_entry_price);

      // If we flip to short
      double remaining_sell = fill_qty - close_qty;
      if (remaining_sell > 0) {
        pos.avg_entry_price = fill_price;
        pos.cost_basis = remaining_sell * fill_price;
      } else if (new_qty == 0) {
        pos.avg_entry_price = 0.0;
        pos.cost_basis = 0.0;
      }
    }
  }

  pos.quantity = new_qty;
}

void PortfolioTracker::update_mark_to_market(const MarketId& market_id, Outcome outcome,
                                             double mid_price) {
  mid_prices_[{market_id, outcome}] = mid_price;
}

void PortfolioTracker::on_market_resolved(const MarketId& market_id, Outcome winning_outcome) {
  PositionKey yes_key{market_id, Outcome::Yes};
  PositionKey no_key{market_id, Outcome::No};

  auto yes_it = positions_.find(yes_key);
  auto no_it = positions_.find(no_key);

  // Process YES position
  if (yes_it != positions_.end() && yes_it->second.quantity != 0.0) {
    double settlement_price = (winning_outcome == Outcome::Yes) ? 1.0 : 0.0;
    Position& pos = yes_it->second;

    if (pos.quantity > 0) {
      // Long position: PnL = (settlement_price - avg_entry_price) * quantity
      realized_pnl_ += pos.quantity * (settlement_price - pos.avg_entry_price);
    } else {
      // Short position: PnL = (avg_entry_price - settlement_price) * |quantity|
      realized_pnl_ += (-pos.quantity) * (pos.avg_entry_price - settlement_price);
    }

    pos.quantity = 0.0;
    pos.avg_entry_price = 0.0;
    pos.cost_basis = 0.0;
  }

  // Process NO position
  if (no_it != positions_.end() && no_it->second.quantity != 0.0) {
    double settlement_price = (winning_outcome == Outcome::No) ? 1.0 : 0.0;
    Position& pos = no_it->second;

    if (pos.quantity > 0) {
      // Long position: PnL = (settlement_price - avg_entry_price) * quantity
      realized_pnl_ += pos.quantity * (settlement_price - pos.avg_entry_price);
    } else {
      // Short position: PnL = (avg_entry_price - settlement_price) * |quantity|
      realized_pnl_ += (-pos.quantity) * (pos.avg_entry_price - settlement_price);
    }

    pos.quantity = 0.0;
    pos.avg_entry_price = 0.0;
    pos.cost_basis = 0.0;
  }

  mid_prices_.erase(yes_key);
  mid_prices_.erase(no_key);
}

double PortfolioTracker::get_unrealized_pnl() const {
  double unrealized = 0.0;

  for (const auto& [key, pos] : positions_) {
    if (pos.quantity == 0.0) continue;

    auto it = mid_prices_.find(key);
    if (it == mid_prices_.end()) continue;

    double mid = it->second;
    if (pos.quantity > 0) {
      // Long position: profit if mid > entry
      unrealized += pos.quantity * (mid - pos.avg_entry_price);
    } else {
      // Short position: profit if mid < entry
      unrealized += (-pos.quantity) * (pos.avg_entry_price - mid);
    }
  }

  return unrealized;
}

void PortfolioTracker::record_equity_snapshot() { equity_history_.push_back(get_total_pnl()); }

double PortfolioTracker::get_sharpe_ratio() const {
  if (equity_history_.size() < 2) {
    return 0.0;  // Not enough data
  }

  // Calculate returns from equity changes
  std::vector<double> returns;
  returns.reserve(equity_history_.size() - 1);

  for (size_t i = 1; i < equity_history_.size(); ++i) {
    returns.push_back(equity_history_[i] - equity_history_[i - 1]);
  }

  // Mean return
  double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
  double mean = sum / returns.size();

  // Standard deviation
  double sq_sum = 0.0;
  for (double r : returns) {
    sq_sum += (r - mean) * (r - mean);
  }
  double std_dev = std::sqrt(sq_sum / returns.size());

  if (std_dev == 0.0) {
    return 0.0;  // No volatility
  }

  return mean / std_dev;
}
