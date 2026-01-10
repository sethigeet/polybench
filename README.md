# Polybench

A high-performance algorithmic trading framework for [Polymarket](https://polymarket.com/) prediction markets. Built with C++20 for low-latency order execution and Python for flexible strategy development.

## ✨ Features

### Core Trading Engine
- **Real-time WebSocket Integration** — Live market data streaming from Polymarket's CLOB (Central Limit Order Book)
- **Virtual Order Matching** — Submit and track virtual orders that simulate fills against live market trades
- **Portfolio Tracking** — Track positions, realized/unrealized PnL, and Sharpe ratio in real-time

### Performance Optimized
- **Zero-copy JSON Parsing** — Ultra-fast message parsing with [simdjson](https://github.com/simdjson/simdjson)
- **Lock-free Ring Buffer** — Efficient message passing between WebSocket and engine threads
- **SmallVector & FixedString** — Stack-allocated containers to minimize heap allocations in hot paths
- **Profile-Guided Optimization (PGO)** — Built-in support for PGO builds for maximum performance

### Strategy Development
- **Python Strategy Interface** — Write strategies in Python while benefiting from C++ execution speed
- **Event-driven Callbacks** — React to book updates, price changes, trades, and order fills
- **Market Data Access** — Query best bid/ask, depth at price levels, and more from your strategy

## 🛠️ Prerequisites

- **CMake** ≥ 3.27
- **Ninja** (build system)
- **C++20 compatible compiler** (GCC 13+ or Clang 17+)
- **Python** 3.10+ with development headers
- **OpenSSL** (for WebSocket TLS support)

## 🚀 Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/sethigeet/algobench.git
cd algobench
```

### 2. Build the Application

```bash
# Debug build (default)
make build

# Release build
make build BUILD_TYPE=Release
```

### 3. Configure Your Markets

Copy the example configuration and edit it with your target market IDs:

```bash
cp config.example.json config.json
```

Configuration format:
```json
{
  "ws_url": "wss://ws-subscriptions-clob.polymarket.com/ws/market",
  "assets": [
    {
      "asset_id": "<ASSET_TOKEN_ID>",
      "market_id": "<MARKET_CONDITION_ID>",
      "outcome": "YES"
    },
    {
      "asset_id": "<ASSET_TOKEN_ID>",
      "market_id": "<MARKET_CONDITION_ID>",
      "outcome": "NO"
    }
  ]
}
```

### 4. Run with a Strategy

```bash
# Run with example configuration
make run

# Run with custom config
make run RUN_CONFIG=config.json
```

## 📁 Project Structure

```
polybench/
├── src/                    # C++ source code
├── strategies/             # Python strategy implementations
├── tests/                  # Unit tests
├── benchmarks/             # Performance benchmarks
└── config.example.json     # Example configuration
```

## 🧪 Development

### Running Tests

```bash
make test
```

### Running Benchmarks

```bash
# Run all benchmarks
make bench

# Run specific benchmarks
make bench BENCH_FILTER="Exchange"
```

### Code Coverage

```bash
make coverage
```

This generates an HTML coverage report and starts a local server at `http://localhost:8000`.

### Profile-Guided Optimization (PGO)

For maximum performance in production:

```bash
# Step 1: Build with instrumentation
make pgo-generate

# Step 2: Run your workload to collect profile data
./build/release/polybench --config config.json

# Step 3: Build optimized binary using collected profile
make pgo-build

# Clean profile data if needed
make pgo-clean
```

## 📝 Writing a Strategy

Create a Python file in `strategies/` that inherits from `Strategy`:

```python
from polybench_core import OrderRequest, Outcome, Side, Strategy, logger

class MyStrategy(Strategy):
    def __init__(self):
        super().__init__()

    def on_book(self, msg):
        """Called when a full order book snapshot is received"""
        market_id = msg.market
        outcome = self.get_outcome(market_id, msg.asset_id)
        
        # Access order book data
        best_bid = self.get_yes_best_bid(market_id)
        best_ask = self.get_yes_best_ask(market_id)
        
        logger.info(f"Book: {best_bid} / {best_ask}")

    def on_price_change(self, msg):
        """Called when prices update (new orders or cancellations)"""
        pass

    def on_trade(self, msg):
        """Called when a trade occurs on the market"""
        pass

    def on_fill(self, fill):
        """Called when your order is filled"""
        logger.info(f"Filled: {fill.filled_quantity} @ {fill.filled_price}")

    def on_market_resolved(self, msg):
        """Called when a market resolves (YES or NO wins)"""
        pass
```

### Strategy API

| Method | Description |
|--------|-------------|
| `submit_order(request)` | Submit a virtual order, returns order ID |
| `cancel_order(market_id, order_id)` | Cancel a pending virtual order |
| `get_yes_best_bid(market_id)` | Get best bid price for YES outcome |
| `get_yes_best_ask(market_id)` | Get best ask price for YES outcome |
| `get_no_best_bid(market_id)` | Get best bid price for NO outcome |
| `get_no_best_ask(market_id)` | Get best ask price for NO outcome |
| `get_yes_bid_depth(market_id, price)` | Get size at bid price for YES |
| `get_yes_ask_depth(market_id, price)` | Get size at ask price for YES |
| `get_outcome(market_id, asset_id)` | Map asset ID to YES/NO outcome |

## 🧹 Clean Build

```bash
make clean
```
