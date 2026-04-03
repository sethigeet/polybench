#include <pybind11/embed.h>
#include <signal.h>

#include "config.hpp"
#include "engine.hpp"
#include "logger.hpp"

namespace py = pybind11;

void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    LOG_INFO("Received shutdown signal ({})", signum);

    // Reset handlers to default for subsequent signals
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    stop_active_engine();
  }
}

int main(int argc, char* argv[]) {
  logger::init();
  LOG_INFO("Initializing PolyBench...");

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  EngineConfig config;
  try {
    config = ConfigLoader::load_from_args(argc, argv);
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to load configuration: {}", e.what());
    ConfigLoader::print_usage();
    return 1;
  }

  if (config.asset_ids.empty()) {
    LOG_ERROR("No assets configured. Use --config or --asset to specify assets to subscribe to.");
    ConfigLoader::print_usage();
    return 1;
  }

  LOG_INFO("Configuration loaded: {} assets to subscribe", config.asset_ids.size());

  // Load and initialize Python Interpreter once
  py::scoped_interpreter guard{};
  int exit_code = 0;

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

    LOG_INFO("Starting engine with live Polymarket data...");
    exit_code = run_engine(strategy, config);
  } catch (py::error_already_set& e) {
    spdlog::get("Python")->error("Python Error: {}", e.what());
    return 1;
  } catch (const std::exception& e) {
    LOG_ERROR("Critical Error: {}", e.what());
    return 1;
  }

  LOG_INFO("PolyBench shutdown complete.");

  // Flush async log queue and join the spdlog background thread
  spdlog::shutdown();

  return exit_code;
}
