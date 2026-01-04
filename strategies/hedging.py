import math
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Optional

from polybench_core import OrderRequest, Outcome, Side, Strategy, logger


@dataclass
class MarketState:
    """Track state for a single market outcome"""

    # Position tracking
    position: float = 0.0  # Positive = long, negative = short

    # Price tracking for EMA
    last_mid: Optional[float] = None
    ema_mid: Optional[float] = None
    ema_alpha: float = 0.15  # EMA decay factor

    # Volatility estimation
    price_history: list = field(default_factory=list)
    volatility: float = 0.02  # Default volatility

    # Order tracking
    active_bid_id: Optional[int] = None
    active_ask_id: Optional[int] = None
    active_bid_price: Optional[float] = None
    active_ask_price: Optional[float] = None

    # Trade statistics
    trades_count: int = 0
    total_pnl: float = 0.0
    total_volume: float = 0.0


class HedgingStrategy(Strategy):
    """
    Market-Making Strategy with Inventory Management

    This strategy aims for a high Sharpe ratio through:
    1. Consistent spread capture via market making
    2. Inventory-adjusted quotes to maintain neutral position
    3. Volatility-adjusted spread widths
    4. Position limits and risk controls
    5. Cross-outcome hedging awareness
    """

    # Strategy Parameters
    BASE_SPREAD = 0.02  # Base bid-ask spread (2 cents)
    MIN_SPREAD = 0.01  # Minimum spread threshold
    MAX_SPREAD = 0.08  # Maximum spread to avoid poor fills
    ORDER_SIZE = 15.0  # Base order size
    MAX_POSITION = 100.0  # Maximum position per outcome
    INVENTORY_SKEW = 0.005  # Price adjustment per unit of inventory
    VOL_SPREAD_MULT = 2.0  # Volatility multiplier for spread
    EDGE_BUFFER = 0.03  # Buffer from price extremes (0.01, 0.99)
    PRICE_HISTORY_LEN = 50  # Number of prices for volatility calc
    MIN_DEPTH_REQUIRED = 5.0  # Minimum book depth to trade
    REQUOTE_THRESHOLD = 0.005  # Min price move to trigger requote

    def __init__(self):
        super().__init__()
        # Market state: {market_id: {outcome: MarketState}}
        self.markets: dict[str, dict[Outcome, MarketState]] = defaultdict(
            lambda: {Outcome.Yes: MarketState(), Outcome.No: MarketState()}
        )
        # Global tracking
        self.total_fills = 0
        self.total_pnl = 0.0

    def on_book(self, msg):
        """Initialize market state from book snapshot"""
        market_id = msg.market
        outcome = self.get_outcome(market_id, msg.asset_id)
        if outcome is None:
            return

        state = self.markets[market_id][outcome]

        # Get best prices
        if outcome == Outcome.Yes:
            best_bid = self.get_yes_best_bid(market_id)
            best_ask = self.get_yes_best_ask(market_id)
        else:
            best_bid = self.get_no_best_bid(market_id)
            best_ask = self.get_no_best_ask(market_id)

        if best_bid is not None and best_ask is not None:
            mid = (best_bid + best_ask) / 2
            state.last_mid = mid
            state.ema_mid = mid if state.ema_mid is None else state.ema_mid
            self._update_volatility(state, mid)

        logger.debug(
            f"Book init: {market_id[:12]}... ({outcome}) "
            f"Bid={best_bid}, Ask={best_ask}, Depth: {len(msg.bids)}x{len(msg.asks)}"
        )

        # Place initial quotes
        self._update_quotes(market_id, outcome)

    def on_price_change(self, msg):
        """React to price changes - core trading logic"""
        market_id = msg.market

        for change in msg.price_changes:
            outcome = self.get_outcome(market_id, change.asset_id)
            if outcome is None:
                continue

            state = self.markets[market_id][outcome]

            # Update price tracking
            if change.best_bid > 0 and change.best_ask > 0:
                mid = (change.best_bid + change.best_ask) / 2

                # Update EMA
                if state.ema_mid is not None:
                    state.ema_mid = (
                        state.ema_alpha * mid + (1 - state.ema_alpha) * state.ema_mid
                    )
                else:
                    state.ema_mid = mid

                self._update_volatility(state, mid)
                state.last_mid = mid

            # Check if we need to requote
            self._maybe_requote(market_id, outcome, change.best_bid, change.best_ask)

    def on_trade(self, msg):
        """Track market trades for information"""
        outcome = self.get_outcome(msg.market, msg.asset_id)
        if outcome is None:
            return

        state = self.markets[msg.market][outcome]

        # Update volatility from trade
        self._update_volatility(state, msg.price)

        logger.debug(
            f"Market trade: {outcome} {msg.side} {msg.size:.1f} @ {msg.price:.4f}"
        )

    def on_fill(self, fill):
        """Handle our order fills - update position and PnL"""
        market_id = fill.market_id
        outcome = fill.outcome
        state = self.markets[market_id][outcome]

        # Update position
        if fill.side == Side.Buy:
            state.position += fill.filled_quantity
            # Mark cost for PnL (simplified)
            state.total_pnl -= fill.filled_price * fill.filled_quantity
        else:
            state.position -= fill.filled_quantity
            state.total_pnl += fill.filled_price * fill.filled_quantity

        state.trades_count += 1
        state.total_volume += fill.filled_quantity
        self.total_fills += 1

        # Clear the filled order from tracking
        if state.active_bid_id == fill.order_id:
            state.active_bid_id = None
            state.active_bid_price = None
        elif state.active_ask_id == fill.order_id:
            state.active_ask_id = None
            state.active_ask_price = None

        logger.info(
            f"FILL: {outcome} {fill.side} {fill.filled_quantity:.1f} @ {fill.filled_price:.4f} "
            f"| Pos: {state.position:+.1f} | Trades: {state.trades_count}"
        )

        # Immediately requote after fill
        self._update_quotes(market_id, outcome)

        # Consider hedging on the other outcome
        self._consider_hedge(market_id, outcome)

    def _update_volatility(self, state: MarketState, price: float):
        """Update rolling volatility estimate"""
        state.price_history.append(price)
        if len(state.price_history) > self.PRICE_HISTORY_LEN:
            state.price_history.pop(0)

        if len(state.price_history) >= 5:
            # Calculate standard deviation of returns
            returns = []
            for i in range(1, len(state.price_history)):
                if state.price_history[i - 1] > 0:
                    ret = (
                        state.price_history[i] - state.price_history[i - 1]
                    ) / state.price_history[i - 1]
                    returns.append(ret)

            if returns:
                mean_ret = sum(returns) / len(returns)
                variance = sum((r - mean_ret) ** 2 for r in returns) / len(returns)
                state.volatility = max(0.005, min(0.1, math.sqrt(variance)))

    def _calculate_fair_value(
        self, market_id: str, outcome: Outcome
    ) -> Optional[float]:
        """Calculate fair value using EMA mid price"""
        state = self.markets[market_id][outcome]

        # Get current book state
        if outcome == Outcome.Yes:
            best_bid = self.get_yes_best_bid(market_id)
            best_ask = self.get_yes_best_ask(market_id)
        else:
            best_bid = self.get_no_best_bid(market_id)
            best_ask = self.get_no_best_ask(market_id)

        if best_bid is None or best_ask is None:
            return state.ema_mid

        current_mid = (best_bid + best_ask) / 2

        # Blend current mid with EMA for smoothing
        if state.ema_mid is not None:
            return 0.7 * current_mid + 0.3 * state.ema_mid

        return current_mid

    def _calculate_spread(self, state: MarketState) -> float:
        """Calculate spread based on volatility and inventory"""
        # Base spread adjusted by volatility
        vol_adjusted = self.BASE_SPREAD + state.volatility * self.VOL_SPREAD_MULT

        # Widen spread if position is large (inventory risk)
        inventory_factor = 1.0 + abs(state.position) / self.MAX_POSITION * 0.5

        spread = vol_adjusted * inventory_factor
        return max(self.MIN_SPREAD, min(self.MAX_SPREAD, spread))

    def _calculate_order_size(self, state: MarketState, is_buy: bool) -> float:
        """Calculate order size with inventory management"""
        base_size = self.ORDER_SIZE

        # Reduce size if near position limit
        remaining_capacity = self.MAX_POSITION - abs(state.position)
        if remaining_capacity <= 0:
            return 0.0

        # Skew size based on position
        if is_buy and state.position > 0:
            # Already long, reduce buy size
            size_factor = max(0.2, 1.0 - state.position / self.MAX_POSITION)
        elif not is_buy and state.position < 0:
            # Already short, reduce sell size
            size_factor = max(0.2, 1.0 + state.position / self.MAX_POSITION)
        elif is_buy and state.position < 0:
            # Short, increase buy to reduce position
            size_factor = min(2.0, 1.0 - state.position / self.MAX_POSITION)
        elif not is_buy and state.position > 0:
            # Long, increase sell to reduce position
            size_factor = min(2.0, 1.0 + state.position / self.MAX_POSITION)
        else:
            size_factor = 1.0

        return min(base_size * size_factor, remaining_capacity)

    def _get_inventory_skew(self, state: MarketState) -> float:
        """Calculate price skew based on inventory"""
        # Positive position -> lower bid/ask to encourage selling to us
        # Negative position -> raise bid/ask to encourage buying from us
        return state.position * self.INVENTORY_SKEW

    def _update_quotes(self, market_id: str, outcome: Outcome):
        """Update bid/ask quotes for a market outcome"""
        state = self.markets[market_id][outcome]

        # Get fair value
        fair_value = self._calculate_fair_value(market_id, outcome)
        if fair_value is None:
            return

        # Skip if too close to edges (high gamma risk)
        if fair_value < self.EDGE_BUFFER or fair_value > (1.0 - self.EDGE_BUFFER):
            return

        # Calculate quotes
        spread = self._calculate_spread(state)
        half_spread = spread / 2

        # Apply inventory skew
        skew = self._get_inventory_skew(state)

        bid_price = round(fair_value - half_spread - skew, 2)
        ask_price = round(fair_value + half_spread - skew, 2)

        # Ensure valid prices
        bid_price = max(0.01, min(0.98, bid_price))
        ask_price = max(0.02, min(0.99, ask_price))

        # Ensure proper spread
        if ask_price <= bid_price:
            ask_price = bid_price + 0.01

        # Calculate sizes
        bid_size = self._calculate_order_size(state, is_buy=True)
        ask_size = self._calculate_order_size(state, is_buy=False)

        # Cancel existing orders if prices changed significantly
        self._cancel_stale_orders(market_id, outcome, bid_price, ask_price)

        # Place bid if size > 0 and no active bid
        if bid_size > 0 and state.active_bid_id is None:
            if state.position < self.MAX_POSITION:  # Position limit check
                request = OrderRequest(
                    market_id, outcome, bid_price, bid_size, Side.Buy
                )
                order_id = self.submit_order(request)
                state.active_bid_id = order_id
                state.active_bid_price = bid_price
                logger.debug(f"Quote BID: {outcome} {bid_size:.1f} @ {bid_price:.4f}")

        # Place ask if size > 0 and no active ask
        if ask_size > 0 and state.active_ask_id is None:
            if state.position > -self.MAX_POSITION:  # Position limit check
                request = OrderRequest(
                    market_id, outcome, ask_price, ask_size, Side.Sell
                )
                order_id = self.submit_order(request)
                state.active_ask_id = order_id
                state.active_ask_price = ask_price
                logger.debug(f"Quote ASK: {outcome} {ask_size:.1f} @ {ask_price:.4f}")

    def _cancel_stale_orders(
        self, market_id: str, outcome: Outcome, new_bid: float, new_ask: float
    ):
        """Cancel orders if prices have moved too much"""
        state = self.markets[market_id][outcome]

        # Cancel bid if price moved significantly
        if state.active_bid_id is not None and state.active_bid_price is not None:
            if abs(state.active_bid_price - new_bid) > self.REQUOTE_THRESHOLD:
                self.cancel_order(market_id, state.active_bid_id)
                state.active_bid_id = None
                state.active_bid_price = None

        # Cancel ask if price moved significantly
        if state.active_ask_id is not None and state.active_ask_price is not None:
            if abs(state.active_ask_price - new_ask) > self.REQUOTE_THRESHOLD:
                self.cancel_order(market_id, state.active_ask_id)
                state.active_ask_id = None
                state.active_ask_price = None

    def _maybe_requote(
        self, market_id: str, outcome: Outcome, best_bid: float, best_ask: float
    ):
        """Check if we should update quotes based on market movement"""
        state = self.markets[market_id][outcome]

        if best_bid <= 0 or best_ask <= 0:
            return

        mid = (best_bid + best_ask) / 2

        # Check if mid has moved enough to warrant requote
        should_requote = False

        if state.last_mid is not None:
            if abs(mid - state.last_mid) > self.REQUOTE_THRESHOLD:
                should_requote = True

        # Check if our quotes are too far from current market
        if state.active_bid_price is not None:
            if state.active_bid_price < best_bid - self.REQUOTE_THRESHOLD * 2:
                should_requote = True

        if state.active_ask_price is not None:
            if state.active_ask_price > best_ask + self.REQUOTE_THRESHOLD * 2:
                should_requote = True

        if should_requote:
            self._update_quotes(market_id, outcome)

    def _consider_hedge(self, market_id: str, filled_outcome: Outcome):
        """Consider hedging on the opposite outcome"""
        yes_state = self.markets[market_id][Outcome.Yes]
        no_state = self.markets[market_id][Outcome.No]

        # Calculate net exposure (YES - NO position approximates directional risk)
        # In Polymarket: YES@p + NO@(1-p) = 1, so they're complementary
        net_exposure = yes_state.position - no_state.position

        # If net exposure is large, consider hedging
        if abs(net_exposure) > self.MAX_POSITION * 0.5:
            # Hedge on the opposite outcome
            hedge_outcome = Outcome.No if net_exposure > 0 else Outcome.Yes

            # Get market prices for hedge
            if hedge_outcome == Outcome.Yes:
                best_ask = self.get_yes_best_ask(market_id)
                if best_ask is not None and best_ask < 0.5:  # Only if reasonably priced
                    hedge_size = min(abs(net_exposure) * 0.3, self.ORDER_SIZE)
                    request = OrderRequest(
                        market_id, hedge_outcome, best_ask, hedge_size, Side.Buy
                    )
                    self.submit_order(request)
                    logger.info(f"HEDGE: Buy YES {hedge_size:.1f} @ {best_ask:.4f}")
            else:
                best_ask = self.get_no_best_ask(market_id)
                if best_ask is not None and best_ask < 0.5:
                    hedge_size = min(abs(net_exposure) * 0.3, self.ORDER_SIZE)
                    request = OrderRequest(
                        market_id, hedge_outcome, best_ask, hedge_size, Side.Buy
                    )
                    self.submit_order(request)
                    logger.info(f"HEDGE: Buy NO {hedge_size:.1f} @ {best_ask:.4f}")
