from polybench_core import OrderRequest, Side, Strategy, logger


class MyStrategy(Strategy):
    def __init__(self):
        super().__init__()

    def on_book(self, msg):
        """Called when a book snapshot is received"""
        asset_id = msg.asset_id
        best_bid = self.get_best_bid(asset_id)
        best_ask = self.get_best_ask(asset_id)
        logger.info(
            f"Book snapshot received for asset {asset_id}. Best Bid: {best_bid}, Best Ask: {best_ask}, "
            f"Bids: {len(msg.bids)}, Asks: {len(msg.asks)}"
        )

    def on_price_change(self, msg):
        """Called when price changes occur which happens when a new order is
        placed or an order is cancelled"""
        for change in msg.price_changes:
            asset_id = change.asset_id
            best_bid = self.get_best_bid(asset_id)
            best_ask = self.get_best_ask(asset_id)
            mid_price = self.get_mid_price(asset_id)
            spread = self.get_spread(asset_id)

            mid_str = f"{mid_price:.4f}" if mid_price is not None else "N/A"
            spread_str = f"{spread:.4f}" if spread is not None else "N/A"

            side_str = "BUY" if change.side == Side.Buy else "SELL"
            logger.debug(
                f"Price change: asset {asset_id} - {side_str} @ {change.price:.4f}, new_size={change.size:.2f}"
            )

            logger.debug(
                f"Live Book [{asset_id[:16]}...]: Bid={best_bid}, Ask={best_ask}, Mid={mid_str}, Spread={spread_str}"
            )

            # Price is probability (0.01 - 0.99)
            if mid_price is not None and spread is not None:
                # If spread is tight (<0.02) and we think probability is underpriced
                if spread < 0.02:
                    if mid_price < 0.50:
                        # Buy YES shares - expect probability to increase
                        logger.info(
                            f"Buying YES at {best_ask:.4f} - tight spread, bullish"
                        )
                        request = OrderRequest(asset_id, best_ask, 10.0, Side.Buy)
                        order_id = self.submit_order(request)
                        logger.debug(f"Order submitted with ID: {order_id}")
                    elif mid_price > 0.50:
                        # Sell YES shares (or buy NO) - expect probability to decrease
                        logger.info(
                            f"Selling YES at {best_bid:.4f} - tight spread, bearish"
                        )
                        request = OrderRequest(asset_id, best_bid, 10.0, Side.Sell)
                        order_id = self.submit_order(request)
                        logger.debug(f"Order submitted with ID: {order_id}")

    def on_trade(self, msg):
        """Called when a trade occurs on the market"""
        side_str = "BUY" if msg.side == Side.Buy else "SELL"
        logger.debug(f"Trade: {side_str} {msg.size:.2f} @ {msg.price:.4f}")

    def on_fill(self, fill):
        logger.info(
            f"Order {fill.order_id} filled @ {fill.filled_price:.4f} (asset: {fill.asset_id[:16]}...)"
        )
