#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "strategy.hpp"
#include "types.hpp"

#define LOGGER_NAME "Python"
#include "logger.hpp"

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(algobench_core, m) {
  // Logging submodule
  auto m_log = m.def_submodule("logger", "Logging utilities");
  m_log.def("info", [](const std::string& msg) { LOG_INFO(msg); });
  m_log.def("warn", [](const std::string& msg) { LOG_WARN(msg); });
  m_log.def("error", [](const std::string& msg) { LOG_ERROR(msg); });
  m_log.def("debug", [](const std::string& msg) { LOG_DEBUG(msg); });

  py::enum_<Side>(m, "Side").value("Bid", Side::Bid).value("Ask", Side::Ask);

  py::class_<MarketTick>(m, "MarketTick")
      .def_readonly("price", &MarketTick::price)
      .def_readonly("quantity", &MarketTick::quantity)
      .def_readonly("side", &MarketTick::side)
      .def_readonly("timestamp", &MarketTick::timestamp);

  py::class_<Order>(m, "Order")
      .def(py::init<uint64_t, double, double, Side, uint64_t>())
      .def_readwrite("id", &Order::id)
      .def_readwrite("price", &Order::price)
      .def_readwrite("quantity", &Order::quantity)
      .def_readwrite("side", &Order::side);

  py::class_<FillReport>(m, "FillReport")
      .def_readonly("order_id", &FillReport::order_id)
      .def_readonly("filled_price", &FillReport::filled_price)
      .def_readonly("filled_quantity", &FillReport::filled_quantity);

  py::class_<Strategy, PyStrategy, std::shared_ptr<Strategy>>(m, "Strategy")
      .def(py::init<>())
      .def("on_tick", &Strategy::on_tick)
      .def("on_fill", &Strategy::on_fill)
      .def("submit_order", &Strategy::submit_order)
      .def("cancel_order", &Strategy::cancel_order)
      .def("get_best_bid", &Strategy::get_best_bid,
           "Get the best bid price, or None if no bids available")
      .def("get_best_ask", &Strategy::get_best_ask,
           "Get the best ask price, or None if no asks available")
      .def("get_mid_price", &Strategy::get_mid_price,
           "Get the mid-price (avg of best bid and ask), or None if not "
           "available")
      .def("get_spread", &Strategy::get_spread, "Get the bid-ask spread, or None if not available")
      .def("get_bid_depth", &Strategy::get_bid_depth, py::arg("price"),
           "Get the quantity available at a specific bid price level")
      .def("get_ask_depth", &Strategy::get_ask_depth, py::arg("price"),
           "Get the quantity available at a specific ask price level");
}
