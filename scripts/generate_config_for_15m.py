# /// script
# dependencies = [
#   "requests",
# ]
# ///

import json
import sys
import time

import requests

if len(sys.argv) < 2:
    print("Usage: python generate_config_for_15m.py <coin>")
    sys.exit(1)

BASE_URL = "https://gamma-api.polymarket.com/markets/slug"
COIN = sys.argv[1]
now = int(time.time())
TIME = now - (now % (15 * 60))
SLUG = f"{COIN}-updown-15m-{TIME}"

response = requests.get(f"{BASE_URL}/{SLUG}")
market = response.json()
clob_token_ids = json.loads(market["clobTokenIds"])

market_id = market["conditionId"]

yes_token_id = clob_token_ids[0]
no_token_id = clob_token_ids[1]

config = {
    "ws_url": "wss://ws-subscriptions-clob.polymarket.com/ws/market",
    "assets": [
        {"asset_id": yes_token_id, "market_id": market_id, "outcome": "YES"},
        {"asset_id": no_token_id, "market_id": market_id, "outcome": "NO"},
    ],
}

with open("config.example.json", "w") as f:
    json.dump(config, f, indent=2)
