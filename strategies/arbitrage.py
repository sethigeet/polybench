from collections import defaultdict
from dataclasses import dataclass
from typing import Optional

from polybench_core import OrderRequest, Outcome, Side, Strategy, logger


@dataclass
class ArbitrageState:
    """Track arbitrage state for a single market"""

    # Last known ask prices
    yes_ask: Optional[float] = None
    no_ask: Optional[float] = None

    # Position tracking (we always buy equal amounts)
    yes_position: float = 0.0
    no_position: float = 0.0

    # Cost tracking for PnL calculation
    total_cost: float = 0.0

    # Order tracking to avoid duplicate orders
    pending_yes_order: Optional[int] = None
    pending_no_order: Optional[int] = None

    # Statistics
    opportunities_found: int = 0
    trades_executed: int = 0
    locked_profit: float = 0.0


class ArbitrageStrategy(Strategy):
    # Strategy Parameters
    MIN_PROFIT_THRESHOLD = 0.005  # Minimum profit margin (0.5 cents) to execute
    ORDER_SIZE = 25.0  # Size per outcome (will buy equal amounts of YES and NO)
    MAX_POSITION = 500.0  # Maximum position per outcome
    MIN_DEPTH_REQUIRED = 1.0  # Minimum liquidity required at the ask

    def __init__(self):
        super().__init__()
        # Market state: {market_id: ArbitrageState}
        self.markets: dict[str, ArbitrageState] = defaultdict(ArbitrageState)
        # Global statistics
        self.total_opportunities = 0
        self.total_profit_locked = 0.0

    def on_book(self, msg):
        """Initialize market state from book snapshot"""
        market_id = msg.market
        outcome = self.get_outcome(market_id, msg.asset_id)
        if outcome is None:
            return

        state = self.markets[market_id]

        # Update ask prices from book
        if outcome == Outcome.Yes:
            best_ask = self.get_yes_best_ask(market_id)
            if best_ask is not None:
                state.yes_ask = best_ask
        else:
            best_ask = self.get_no_best_ask(market_id)
            if best_ask is not None:
                state.no_ask = best_ask

        logger.debug(
            f"Book init: {market_id[:12]}... ({outcome}) "
            f"YES_ask={state.yes_ask}, NO_ask={state.no_ask}"
        )

        # Check for arbitrage opportunity
        self._check_arbitrage(market_id)

    def on_price_change(self, msg):
        """React to price changes - main arbitrage detection logic"""
        market_id = msg.market
        state = self.markets[market_id]

        for change in msg.price_changes:
            outcome = self.get_outcome(market_id, change.asset_id)
            if outcome is None:
                continue

            # Update our tracked ask prices
            if outcome == Outcome.Yes:
                if change.best_ask > 0:
                    state.yes_ask = change.best_ask
            else:
                if change.best_ask > 0:
                    state.no_ask = change.best_ask

        # Check for arbitrage opportunity after price update
        self._check_arbitrage(market_id)

    def on_trade(self, msg):
        """Track market trades - may indicate ask prices changed"""
        market_id = msg.market
        outcome = self.get_outcome(market_id, msg.asset_id)
        if outcome is None:
            return

        state = self.markets[market_id]

        # Refresh ask prices after a trade
        if outcome == Outcome.Yes:
            best_ask = self.get_yes_best_ask(market_id)
            if best_ask is not None:
                state.yes_ask = best_ask
        else:
            best_ask = self.get_no_best_ask(market_id)
            if best_ask is not None:
                state.no_ask = best_ask

        logger.debug(
            f"Trade observed: {outcome} {msg.side} {msg.size:.1f} @ {msg.price:.4f}"
        )

        # Re-check arbitrage after trade
        self._check_arbitrage(market_id)

    def on_fill(self, fill):
        """Handle our order fills - track position and cost"""
        market_id = fill.market_id
        outcome = fill.outcome
        state = self.markets[market_id]

        # Update position and cost
        cost = fill.filled_price * fill.filled_quantity

        if outcome == Outcome.Yes:
            state.yes_position += fill.filled_quantity
            state.pending_yes_order = None
        else:
            state.no_position += fill.filled_quantity
            state.pending_no_order = None

        state.total_cost += cost
        state.trades_executed += 1

        # Calculate locked profit when we have matching positions
        matched_position = min(state.yes_position, state.no_position)
        if matched_position > 0:
            # Profit locked = matched shares * ($1 - avg cost per pair)
            # This is simplified - actual calc would track per-trade costs
            pass

        logger.info(
            f"FILL: {outcome} BUY {fill.filled_quantity:.1f} @ {fill.filled_price:.4f} "
            f"| YES_pos: {state.yes_position:.1f}, NO_pos: {state.no_position:.1f} "
            f"| Total cost: ${state.total_cost:.2f}"
        )

        # Log current arbitrage position status
        self._log_position_status(market_id)

    def on_market_resolved(self, msg):
        """Handle market resolution - calculate realized profit"""
        market_id = msg.market
        state = self.markets[market_id]

        winning_outcome = msg.winning_outcome
        if winning_outcome == Outcome.Yes:
            payout = state.yes_position * 1.0  # $1 per winning share
        else:
            payout = state.no_position * 1.0

        profit = payout - state.total_cost

        logger.info(
            f"MARKET RESOLVED: {market_id[:12]}... Winner: {winning_outcome} "
            f"| Payout: ${payout:.2f}, Cost: ${state.total_cost:.2f} "
            f"| PROFIT: ${profit:.2f}"
        )

        self.total_profit_locked += profit

    def _check_arbitrage(self, market_id: str):
        """Check if an arbitrage opportunity exists and execute if profitable"""
        state = self.markets[market_id]

        # Need both prices to evaluate opportunity
        if state.yes_ask is None or state.no_ask is None:
            return

        # Calculate total cost to buy both outcomes
        total_ask = state.yes_ask + state.no_ask

        # Arbitrage exists if total < $1.00
        profit_per_share = 1.0 - total_ask

        if profit_per_share < self.MIN_PROFIT_THRESHOLD:
            return  # Not profitable enough

        # Check if we already have pending orders
        if state.pending_yes_order is not None or state.pending_no_order is not None:
            return

        # Check position limits
        if (
            state.yes_position >= self.MAX_POSITION
            or state.no_position >= self.MAX_POSITION
        ):
            logger.debug(f"Position limit reached for {market_id[:12]}...")
            return

        # Check liquidity - ensure enough depth at the ask
        yes_depth = self.get_yes_ask_depth(market_id, state.yes_ask)
        no_depth = self.get_no_ask_depth(market_id, state.no_ask)

        if yes_depth < self.MIN_DEPTH_REQUIRED or no_depth < self.MIN_DEPTH_REQUIRED:
            logger.debug(
                f"Insufficient liquidity: YES_depth={yes_depth:.1f}, NO_depth={no_depth:.1f}"
            )
            return

        # Calculate order size (limited by available depth and position limit)
        remaining_capacity = min(
            self.MAX_POSITION - state.yes_position,
            self.MAX_POSITION - state.no_position,
        )
        order_size = min(self.ORDER_SIZE, yes_depth, no_depth, remaining_capacity)

        if order_size < 1.0:
            return

        # Log the opportunity
        state.opportunities_found += 1
        self.total_opportunities += 1

        expected_profit = profit_per_share * order_size
        logger.info(
            f"ARBITRAGE OPPORTUNITY #{self.total_opportunities}: {market_id[:12]}... "
            f"| YES_ask={state.yes_ask:.4f} + NO_ask={state.no_ask:.4f} = {total_ask:.4f} "
            f"| Profit/share: ${profit_per_share:.4f} ({profit_per_share * 100:.2f}%) "
            f"| Size: {order_size:.1f} | Expected profit: ${expected_profit:.2f}"
        )

        # Execute arbitrage - buy both outcomes simultaneously
        self._execute_arbitrage(market_id, order_size)

    def _execute_arbitrage(self, market_id: str, size: float):
        """Execute the arbitrage by buying both YES and NO outcomes"""
        state = self.markets[market_id]

        # Safety check (should always pass - caller verifies these)
        if state.yes_ask is None or state.no_ask is None:
            return

        yes_ask = state.yes_ask
        no_ask = state.no_ask

        # Buy YES at the ask
        yes_request = OrderRequest(market_id, Outcome.Yes, yes_ask, size, Side.Buy)
        state.pending_yes_order = self.submit_order(yes_request)

        logger.debug(
            f"Submitted YES buy: {size:.1f} @ {yes_ask:.4f} "
            f"(order_id: {state.pending_yes_order})"
        )

        # Buy NO at the ask
        no_request = OrderRequest(market_id, Outcome.No, no_ask, size, Side.Buy)
        state.pending_no_order = self.submit_order(no_request)

        logger.debug(
            f"Submitted NO buy: {size:.1f} @ {no_ask:.4f} "
            f"(order_id: {state.pending_no_order})"
        )

        state.locked_profit += (1.0 - yes_ask - no_ask) * size

    def _log_position_status(self, market_id: str):
        """Log current position and expected profit"""
        state = self.markets[market_id]

        # Matched pairs = min of both positions (complete hedges)
        matched_pairs = min(state.yes_position, state.no_position)

        # Guaranteed payout from matched pairs
        guaranteed_payout = matched_pairs * 1.0

        # Expected profit (simplified - assumes equal cost distribution)
        if state.yes_position > 0 and state.no_position > 0:
            expected_profit = guaranteed_payout - state.total_cost
            avg_cost_per_pair = (
                state.total_cost / matched_pairs if matched_pairs > 0 else 0
            )

            logger.info(
                f"Position status: {market_id[:12]}... "
                f"| Matched pairs: {matched_pairs:.1f} "
                f"| Avg cost/pair: ${avg_cost_per_pair:.4f} "
                f"| Guaranteed payout: ${guaranteed_payout:.2f} "
                f"| Expected profit: ${expected_profit:.2f}"
            )
