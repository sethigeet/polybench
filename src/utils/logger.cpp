#include "utils/logger.hpp"

#include <spdlog/async.h>
#include <spdlog/cfg/helpers.h>
#include <spdlog/details/os.h>
#include <spdlog/details/registry.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <cstdlib>
#include <mutex>

namespace logger {
bool is_fancy_enabled() {
  const char* env = std::getenv("POLYBENCH_PLAIN_LOGS");
  if (env && std::string(env) == "1") {
    return false;
  }
  return true;
}

inline void load_env_levels() {
  auto env_val = spdlog::details::os::getenv("POLYBENCH_LOG_LEVEL");
  LOG_DEBUG("Loading env levels: {}", env_val);
  if (!env_val.empty()) {
    spdlog::cfg::helpers::load_levels(env_val);
  }
}

void init() {
  static std::once_flag once;
  std::call_once(once, [] {
    // Initialize spdlog async thread pool: 8K slot queue, 1 background writer thread.
    // All loggers enqueue messages and return immediately; I/O happens off the hot path.
    spdlog::init_thread_pool(8192, 1);

    bool fancy = is_fancy_enabled();
    std::array names = {"Engine", "Exchange", "Python", "System", "WebSocket", "JsonParser", "Perf"};

    for (const std::string& name : names) {
      // Create sink
      spdlog::sink_ptr sink;
      std::string pattern;
      if (fancy) {
        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        pattern = "[%n] [%H:%M:%S.%e] [%^%l%$] %v";
      } else {
        sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";
      }

      // Create async logger: enqueue + return immediately, overrun_oldest if queue full
      auto l = std::make_shared<spdlog::async_logger>(
          name, sink, spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
      l->set_pattern(pattern);
      l->set_level(DEFAULT_LOG_LEVEL);
      spdlog::register_logger(l);
    }

    // Flush async loggers periodically so output appears in a timely fashion
    spdlog::flush_every(std::chrono::seconds(1));

    load_env_levels();
  });
}

spdlog::logger& get(const std::string& name) {
  if (auto logger = spdlog::get(name)) return *logger;

  // Fallback: lazily create as async (non-blocking) to match init() loggers
  spdlog::sink_ptr sink;
  if (is_fancy_enabled()) {
    sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  } else {
    sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
  }
  auto l = std::make_shared<spdlog::async_logger>(
      name, sink, spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
  spdlog::register_logger(l);
  return *l;
}
}  // namespace logger
