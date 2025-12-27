#include <pybind11/embed.h>

#include "engine.hpp"
#include "logger.hpp"

namespace py = pybind11;

int main() {
  logger::init();
  LOG_INFO("Initializing PolyBench...");

  // Load and initialize Python Interpreter once
  py::scoped_interpreter guard{};

  try {
    // Add 'strategies' directory to Python path
    py::module_ sys = py::module_::import("sys");
    py::module_ os = py::module_::import("os");
    std::string strategy_path = py::str(os.attr("getcwd")()).cast<std::string>() + "/strategies";
    sys.attr("path").attr("append")(strategy_path);

    // Load the Python module
    py::module_ strategy_module = py::module_::import("strategy");
    // Instantiate the Python class
    py::object py_strat_obj = strategy_module.attr("MyStrategy")();

    // Pybind11 will keep the python object alive as long as the shared_ptr
    // exists so we can cast it to a C++ Strategy shared_ptr
    auto strategy = py_strat_obj.cast<std::shared_ptr<Strategy>>();

    Engine engine(strategy);
    engine.run();
  } catch (py::error_already_set &e) {
    spdlog::get("Python")->error("Python Error: {}", e.what());
    return 1;
  } catch (const std::exception &e) {
    LOG_ERROR("Critical Error: {}", e.what());
    return 1;
  }

  return 0;
}
