#include "logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <cstdlib>
#include <mutex>

namespace logger {

bool is_fancy_enabled() {
  const char* env = std::getenv("ALGOBENCH_PLAIN_LOGS");
  if (env && std::string(env) == "1") {
    return false;
  }
  return true;
}

void init() {
  static std::once_flag once;
  std::call_once(once, [] {
    bool fancy = is_fancy_enabled();
    auto names = {"Engine", "Exchange", "Python", "System"};

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