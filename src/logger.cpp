#include "logger.hpp"

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
    bool fancy = is_fancy_enabled();
    auto names = {"Engine", "Exchange", "Python", "System", "WebSocket", "JsonParser"};

    for (const std::string& name : names) {
      std::shared_ptr<spdlog::logger> l;

      if (fancy) {
        l = spdlog::stdout_color_mt(name);
        // Reverting to the very simple format: [Name] [Time] [Level] Message
        l->set_pattern("[%n] [%H:%M:%S.%e] [%^%l%$] %v");
      } else {
        l = spdlog::stdout_logger_mt(name);
        l->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      }
      l->set_level(DEFAULT_LOG_LEVEL);
    }

    load_env_levels();
  });
}

spdlog::logger& get(const std::string& name) {
  if (auto logger = spdlog::get(name)) return *logger;

  if (is_fancy_enabled()) {
    return *spdlog::stdout_color_mt(name);
  } else {
    return *spdlog::stdout_logger_mt(name);
  }
}
}  // namespace logger
