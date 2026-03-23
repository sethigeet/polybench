#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "strategy.hpp"
#include "types/common.hpp"
#include "types/fixed_string.hpp"
#include "types/polymarket.hpp"
#include "types/small_vector.hpp"

#define LOGGER_NAME "Python"
#include "logger.hpp"

namespace py = pybind11;

// Custom type caster for FixedString<N> to Python str
// Since FixedString implicitly converts to string_view, we just need to handle the conversion
namespace pybind11 {
namespace detail {

template <size_t N>
struct type_caster<FixedString<N>> {
 public:
  PYBIND11_TYPE_CASTER(FixedString<N>, const_name("str"));

  // Python str -> FixedString
  bool load(handle src, bool) {
    if (!py::isinstance<py::str>(src)) return false;
    value = FixedString<N>(src.cast<std::string_view>());
    return true;
  }

  // FixedString -> Python str (uses implicit string_view conversion)
  static handle cast(const FixedString<N>& src, return_value_policy, handle) {
    return py::str(std::string_view(src)).release();
  }
};

// Custom type caster for SmallVector<T, N> to Python list
template <typename T, size_t N>
struct type_caster<SmallVector<T, N>> {
 public:
  using value_type = SmallVector<T, N>;
  PYBIND11_TYPE_CASTER(value_type, const_name("list[") + make_caster<T>::name + const_name("]"));

  // Python list -> SmallVector
  bool load(handle src, bool convert) {
    if (!py::isinstance<py::sequence>(src)) return false;
    auto seq = py::reinterpret_borrow<py::sequence>(src);
    value.clear();
    for (auto item : seq) {
      make_caster<T> conv;
      if (!conv.load(item, convert)) return false;
      value.push_back(cast_op<T>(std::move(conv)));
    }
    return true;
  }

  // SmallVector -> Python list
  static handle cast(const value_type& src, return_value_policy policy, handle parent) {
    py::list result;
    for (const auto& item : src) {
      result.append(py::cast(item, policy, parent));
    }
    return result.release();
  }
};

}  // namespace detail
}  // namespace pybind11

// Mark high-frequency SmallVector types as opaque so they are exposed as
// lazy sequence wrappers instead of eagerly converted to Python lists.
// SmallVector<AssetId, 2> is left non-opaque (rare, small).
PYBIND11_MAKE_OPAQUE(OrderList);
PYBIND11_MAKE_OPAQUE(PriceChangeList);

// Registers a SmallVector<T, N> as a read-only Python sequence whose elements
// are converted lazily (on __getitem__ / __iter__) rather than eagerly to a list.
template <typename T, size_t N>
void bind_lazy_small_vector(py::module_ &m, const char *name) {
  using Vec = SmallVector<T, N>;
  py::class_<Vec>(m, name)
      .def("__len__", [](const Vec &v) { return v.size(); })
      .def(
          "__getitem__",
          [](const Vec &v, py::ssize_t i) -> const T & {
            if (i < 0) i += static_cast<py::ssize_t>(v.size());
            if (i < 0 || static_cast<size_t>(i) >= v.size()) throw py::index_error();
            return v[static_cast<size_t>(i)];
          },
          py::return_value_policy::reference_internal)
      .def(
          "__iter__",
          [](const Vec &v) { return py::make_iterator(v.begin(), v.end()); },
          py::keep_alive<0, 1>());
}

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

  bind_lazy_small_vector<OrderSummary, 20>(m, "OrderList");

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

  bind_lazy_small_vector<PriceChange, 2>(m, "PriceChangeList");

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
      .def(py::init<MarketId, Outcome, double, double, Side>(), py::arg("market_id"),
           py::arg("outcome"), py::arg("price"), py::arg("quantity"), py::arg("side"))
      .def_readwrite("market_id", &OrderRequest::market_id)
      .def_readwrite("outcome", &OrderRequest::outcome)
      .def_readwrite("price", &OrderRequest::price)
      .def_readwrite("quantity", &OrderRequest::quantity)
      .def_readwrite("side", &OrderRequest::side);

  py::class_<Order>(m, "Order")
      .def(py::init<MarketId, Outcome, uint64_t, double, double, Side, uint64_t>())
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
