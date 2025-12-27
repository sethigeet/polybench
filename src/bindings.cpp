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

  // Order summary for book snapshots
  py::class_<OrderSummary>(m, "OrderSummary")
      .def_readonly("price", &OrderSummary::price)
      .def_readonly("size", &OrderSummary::size);

  // Book snapshot message
  py::class_<BookMessage>(m, "BookMessage")
      .def_readonly("asset_id", &BookMessage::asset_id)
      .def_readonly("market", &BookMessage::market)
      .def_readonly("bids", &BookMessage::bids)
      .def_readonly("asks", &BookMessage::asks)
      .def_readonly("timestamp", &BookMessage::timestamp)
      .def_readonly("hash", &BookMessage::hash);

  // Single price change
  py::class_<PriceChange>(m, "PriceChange")
      .def_readonly("asset_id", &PriceChange::asset_id)
      .def_readonly("price", &PriceChange::price)
      .def_readonly("size", &PriceChange::size)
      .def_readonly("side", &PriceChange::side)
      .def_readonly("hash", &PriceChange::hash)
      .def_readonly("best_bid", &PriceChange::best_bid)
      .def_readonly("best_ask", &PriceChange::best_ask);

  // Price change message
  py::class_<PriceChangeMessage>(m, "PriceChangeMessage")
      .def_readonly("market", &PriceChangeMessage::market)
      .def_readonly("price_changes", &PriceChangeMessage::price_changes)
      .def_readonly("timestamp", &PriceChangeMessage::timestamp);

  // Last trade message (trade tape)
  py::class_<LastTradeMessage>(m, "LastTradeMessage")
      .def_readonly("asset_id", &LastTradeMessage::asset_id)
      .def_readonly("market", &LastTradeMessage::market)
      .def_readonly("price", &LastTradeMessage::price)
      .def_readonly("side", &LastTradeMessage::side)
      .def_readonly("size", &LastTradeMessage::size)
      .def_readonly("fee_rate_bps", &LastTradeMessage::fee_rate_bps)
      .def_readonly("timestamp", &LastTradeMessage::timestamp);

  py::class_<OrderRequest>(m, "OrderRequest")
      .def(py::init<std::string, double, double, Side>(), py::arg("asset_id"), py::arg("price"),
           py::arg("quantity"), py::arg("side"))
      .def_readwrite("asset_id", &OrderRequest::asset_id)
      .def_readwrite("price", &OrderRequest::price)
      .def_readwrite("quantity", &OrderRequest::quantity)
      .def_readwrite("side", &OrderRequest::side);

  py::class_<Order>(m, "Order")
      .def(py::init<std::string, uint64_t, double, double, Side, uint64_t>())
      .def_readonly("asset_id", &Order::asset_id)
      .def_readonly("id", &Order::id)
      .def_readonly("price", &Order::price)
      .def_readonly("quantity", &Order::quantity)
      .def_readonly("side", &Order::side);

  py::class_<FillReport>(m, "FillReport")
      .def_readonly("asset_id", &FillReport::asset_id)
      .def_readonly("order_id", &FillReport::order_id)
      .def_readonly("filled_price", &FillReport::filled_price)
      .def_readonly("filled_quantity", &FillReport::filled_quantity);

  py::class_<Strategy, PyStrategy, std::shared_ptr<Strategy>>(m, "Strategy")
      .def(py::init<>())
      .def("on_book", &Strategy::on_book)
      .def("on_price_change", &Strategy::on_price_change)
      .def("on_trade", &Strategy::on_trade)
      .def("on_fill", &Strategy::on_fill)
      .def("submit_order", &Strategy::submit_order, py::arg("request"),
           "Submit an order using OrderRequest. Returns the auto-generated order ID.")
      .def("cancel_order", &Strategy::cancel_order, py::arg("asset_id"), py::arg("order_id"),
           "Cancel an order by asset_id and order_id")
      .def("get_best_bid", &Strategy::get_best_bid, py::arg("asset_id"),
           "Get the best bid price for an asset, or None if no bids available")
      .def("get_best_ask", &Strategy::get_best_ask, py::arg("asset_id"),
           "Get the best ask price for an asset, or None if no asks available")
      .def("get_mid_price", &Strategy::get_mid_price, py::arg("asset_id"),
           "Get the mid-price for an asset, or None if not available")
      .def("get_spread", &Strategy::get_spread, py::arg("asset_id"),
           "Get the bid-ask spread for an asset, or None if not available")
      .def("get_bid_depth", &Strategy::get_bid_depth, py::arg("asset_id"), py::arg("price"),
           "Get the quantity available at a specific bid price level for an asset")
      .def("get_ask_depth", &Strategy::get_ask_depth, py::arg("asset_id"), py::arg("price"),
           "Get the quantity available at a specific ask price level for an asset");
}
