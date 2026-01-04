from polybench_core import OrderRequest, Outcome, Side, Strategy, logger


class SimpleStrategy(Strategy):
    def __init__(self):
        super().__init__()

    def on_book(self, msg):
        """Called when a book snapshot is received"""
        market_id = msg.market
        outcome = self.get_outcome(market_id, msg.asset_id)
        if outcome is None:
            logger.warn(f"Unknown asset_id {msg.asset_id}")
            return

        if outcome == Outcome.Yes:
            best_bid = self.get_yes_best_bid(market_id)
            best_ask = self.get_yes_best_ask(market_id)
        else:
            best_bid = self.get_no_best_bid(market_id)
            best_ask = self.get_no_best_ask(market_id)

        logger.debug(
            f"Book snapshot received for market {market_id[:16]}... ({outcome}). "
            f"Best Bid: {best_bid}, Best Ask: {best_ask}, "
            f"Bids: {len(msg.bids)}, Asks: {len(msg.asks)}"
        )

    def on_price_change(self, msg):
        """Called when price changes occur which happens when a new order is
        placed or an order is cancelled"""
        market_id = msg.market

        for change in msg.price_changes:
            outcome = self.get_outcome(market_id, change.asset_id)
            if outcome is None:
                continue

            if outcome == Outcome.Yes:
                best_bid = self.get_yes_best_bid(market_id)
                best_ask = self.get_yes_best_ask(market_id)
            else:
                best_bid = self.get_no_best_bid(market_id)
                best_ask = self.get_no_best_ask(market_id)

            side_str = "BUY" if change.side == Side.Buy else "SELL"
            logger.debug(
                f"Price change: market {market_id[:16]}... ({outcome}) - "
                f"{side_str} @ {change.price:.4f}, new_size={change.size:.2f}"
            )

            logger.debug(f"Live Book [{outcome}]: Bid={best_bid}, Ask={best_ask}")

            # Trading logic example: when spread is tight and mid is away from 0.50
            if best_bid is not None and best_ask is not None:
                mid_price = (best_bid + best_ask) / 2
                spread = best_ask - best_bid

                if spread < 0.02:
                    if mid_price < 0.50:
                        # Buy this outcome - expect probability to increase
                        logger.info(
                            f"Buying {outcome} at {best_ask:.4f} - tight spread, bullish"
                        )
                        request = OrderRequest(
                            market_id, outcome, best_ask, 10.0, Side.Buy
                        )
                        order_id = self.submit_order(request)
                        logger.debug(f"Order submitted with ID: {order_id}")
                    elif mid_price > 0.50:
                        # Sell this outcome - expect probability to decrease
                        logger.info(
                            f"Selling {outcome} at {best_bid:.4f} - tight spread, bearish"
                        )
                        request = OrderRequest(
                            market_id, outcome, best_bid, 10.0, Side.Sell
                        )
                        order_id = self.submit_order(request)
                        logger.debug(f"Order submitted with ID: {order_id}")

    def on_trade(self, msg):
        """Called when a trade occurs on the market"""
        outcome = self.get_outcome(msg.market, msg.asset_id)
        logger.debug(f"Trade ({outcome}): {msg.side} {msg.size:.2f} @ {msg.price:.4f}")

    def on_fill(self, fill):
        logger.info(
            f"Order {fill.order_id} filled @ {fill.filled_price:.4f} "
            f"(market: {fill.market_id[:16]}..., outcome: {fill.outcome})"
        )
