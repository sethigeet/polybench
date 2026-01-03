#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "strategy.hpp"
#include "types/common.hpp"
#include "types/polymarket.hpp"

#define LOGGER_NAME "Python"
#include "logger.hpp"

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(polybench_core, m) {
  // Logging submodule
  auto m_log = m.def_submodule("logger", "Logging utilities");
  m_log.def("info", [](const std::string& msg) { LOG_INFO(msg); });
  m_log.def("warn", [](const std::string& msg) { LOG_WARN(msg); });
  m_log.def("error", [](const std::string& msg) { LOG_ERROR(msg); });
  m_log.def("debug", [](const std::string& msg) { LOG_DEBUG(msg); });

  py::enum_<Side>(m, "Side").value("Buy", Side::Buy).value("Sell", Side::Sell);

  py::enum_<Outcome>(m, "Outcome").value("Yes", Outcome::Yes).value("No", Outcome::No);

  py::class_<OrderSummary>(m, "OrderSummary")
      .def_readonly("price", &OrderSummary::price)
      .def_readonly("size", &OrderSummary::size);

  py::class_<BookMessage>(m, "BookMessage")
      .def_readonly("asset_id", &BookMessage::asset_id)
      .def_readonly("market", &BookMessage::market)
      .def_readonly("bids", &BookMessage::bids)
      .def_readonly("asks", &BookMessage::asks)
      .def_readonly("timestamp", &BookMessage::timestamp);

  py::class_<PriceChange>(m, "PriceChange")
      .def_readonly("asset_id", &PriceChange::asset_id)
      .def_readonly("price", &PriceChange::price)
      .def_readonly("size", &PriceChange::size)
      .def_readonly("side", &PriceChange::side)
      .def_readonly("best_bid", &PriceChange::best_bid)
      .def_readonly("best_ask", &PriceChange::best_ask);

  py::class_<PriceChangeMessage>(m, "PriceChangeMessage")
      .def_readonly("market", &PriceChangeMessage::market)
      .def_readonly("price_changes", &PriceChangeMessage::price_changes)
      .def_readonly("timestamp", &PriceChangeMessage::timestamp);

  py::class_<LastTradeMessage>(m, "LastTradeMessage")
      .def_readonly("asset_id", &LastTradeMessage::asset_id)
      .def_readonly("market", &LastTradeMessage::market)
      .def_readonly("price", &LastTradeMessage::price)
      .def_readonly("side", &LastTradeMessage::side)
      .def_readonly("size", &LastTradeMessage::size)
      .def_readonly("fee_rate_bps", &LastTradeMessage::fee_rate_bps)
      .def_readonly("timestamp", &LastTradeMessage::timestamp);

  py::class_<MarketResolvedMessage>(m, "MarketResolvedMessage")
      .def_readonly("market", &MarketResolvedMessage::market)
      .def_readonly("winning_asset_id", &MarketResolvedMessage::winning_asset_id)
      .def_readonly("winning_outcome", &MarketResolvedMessage::winning_outcome)
      .def_readonly("asset_ids", &MarketResolvedMessage::asset_ids)
      .def_readonly("timestamp", &MarketResolvedMessage::timestamp);

  py::class_<OrderRequest>(m, "OrderRequest")
      .def(py::init<std::string, Outcome, double, double, Side>(), py::arg("market_id"),
           py::arg("outcome"), py::arg("price"), py::arg("quantity"), py::arg("side"))
      .def_readwrite("market_id", &OrderRequest::market_id)
      .def_readwrite("outcome", &OrderRequest::outcome)
      .def_readwrite("price", &OrderRequest::price)
      .def_readwrite("quantity", &OrderRequest::quantity)
      .def_readwrite("side", &OrderRequest::side);

  py::class_<Order>(m, "Order")
      .def(py::init<std::string, Outcome, uint64_t, double, double, Side, uint64_t>())
      .def_readonly("market_id", &Order::market_id)
      .def_readonly("outcome", &Order::outcome)
      .def_readonly("id", &Order::id)
      .def_readonly("price", &Order::price)
      .def_readonly("quantity", &Order::quantity)
      .def_readonly("side", &Order::side);

  py::class_<FillReport>(m, "FillReport")
      .def_readonly("market_id", &FillReport::market_id)
      .def_readonly("outcome", &FillReport::outcome)
      .def_readonly("order_id", &FillReport::order_id)
      .def_readonly("filled_price", &FillReport::filled_price)
      .def_readonly("filled_quantity", &FillReport::filled_quantity)
      .def_readonly("side", &FillReport::side);

  py::class_<Strategy, PyStrategy, std::shared_ptr<Strategy>>(m, "Strategy")
      .def(py::init<>())
      .def("on_book", &Strategy::on_book)
      .def("on_price_change", &Strategy::on_price_change)
      .def("on_trade", &Strategy::on_trade)
      .def("on_fill", &Strategy::on_fill)
      .def("on_market_resolved", &Strategy::on_market_resolved)
      .def("submit_order", &Strategy::submit_order, py::arg("request"),
           "Submit an order using OrderRequest. Returns the auto-generated order ID.")
      .def("cancel_order", &Strategy::cancel_order, py::arg("market_id"), py::arg("order_id"),
           "Cancel an order by market_id and order_id")
      .def("get_outcome", &Strategy::get_outcome, py::arg("market_id"), py::arg("asset_id"),
           "Get the Outcome (Yes/No) for an asset_id in a market")
      // YES side getters
      .def("get_yes_best_bid", &Strategy::get_yes_best_bid, py::arg("market_id"),
           "Get the best YES bid price for a market, or None if no bids available")
      .def("get_yes_best_ask", &Strategy::get_yes_best_ask, py::arg("market_id"),
           "Get the best YES ask price for a market, or None if no asks available")
      .def("get_yes_bid_depth", &Strategy::get_yes_bid_depth, py::arg("market_id"),
           py::arg("price"), "Get the YES bid quantity at a specific price level")
      .def("get_yes_ask_depth", &Strategy::get_yes_ask_depth, py::arg("market_id"),
           py::arg("price"), "Get the YES ask quantity at a specific price level")
      // NO side getters
      .def("get_no_best_bid", &Strategy::get_no_best_bid, py::arg("market_id"),
           "Get the best NO bid price for a market, or None if no bids available")
      .def("get_no_best_ask", &Strategy::get_no_best_ask, py::arg("market_id"),
           "Get the best NO ask price for a market, or None if no asks available")
      .def("get_no_bid_depth", &Strategy::get_no_bid_depth, py::arg("market_id"), py::arg("price"),
           "Get the NO bid quantity at a specific price level")
      .def("get_no_ask_depth", &Strategy::get_no_ask_depth, py::arg("market_id"), py::arg("price"),
           "Get the NO ask quantity at a specific price level");
}
