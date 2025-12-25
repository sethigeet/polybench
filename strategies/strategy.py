from algobench_core import Order, Side, Strategy, logger


class MyStrategy(Strategy):
    def __init__(self):
        super().__init__()
        self.order_id_counter = 1
        logger.info("MyStrategy Initialized")

    def on_tick(self, tick):
        best_bid = self.get_best_bid()
        best_ask = self.get_best_ask()
        mid_price = self.get_mid_price()
        spread = self.get_spread()

        mid_str = f"{mid_price:.2f}" if mid_price is not None else "N/A"
        spread_str = f"{spread:.4f}" if spread is not None else "N/A"

        logger.debug(f"on_tick: Price={tick.price} Size={tick.quantity}")
        logger.debug(
            f"Book State: Bid={best_bid}, Ask={best_ask}, Mid={mid_str}, Spread={spread_str}"
        )

        # Strategy logic: Buy when spread is tight and price is below mid
        if mid_price is not None and spread is not None:
            if spread < 2.0 and tick.price < mid_price:
                logger.info("Buying - tight spread and price below mid!")
                self.submit_order(
                    Order(
                        self.order_id_counter, tick.price, 1.0, Side.Bid, tick.timestamp
                    )
                )
                self.order_id_counter += 1
        elif tick.price < 100.0:
            # Fallback strategy: Buy aggressively if no book data is available
            logger.info("Buying aggressively (no book data)!")
            self.submit_order(
                Order(self.order_id_counter, tick.price, 1.0, Side.Bid, tick.timestamp)
            )
            self.order_id_counter += 1

    def on_fill(self, fill):
        logger.info(f"on_fill: Order {fill.order_id} filled @ {fill.filled_price}")
