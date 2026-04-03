#pragma once

#include <spdlog/async_logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <string>

#ifdef NDEBUG
#define DEFAULT_LOG_LEVEL spdlog::level::info
#else
#define DEFAULT_LOG_LEVEL spdlog::level::debug
#endif

namespace logger {
void init();
spdlog::logger& get(const std::string& name);

// Helper to check if we should use fancy logging
bool is_fancy_enabled();
}  // namespace logger

#ifndef LOGGER_NAME
#define LOGGER_NAME "System"
#endif

// Cached logger pointer macros: the spdlog registry lookup happens exactly once
// per call site (C++11 magic statics, thread-safe). All subsequent calls use the
// cached raw pointer with zero overhead.
#define LOG_TRACE(...)                                          \
  do {                                                          \
    static spdlog::logger* cached = &logger::get(LOGGER_NAME); \
    cached->trace(__VA_ARGS__);                                 \
  } while (0)
#define LOG_DEBUG(...)                                          \
  do {                                                          \
    static spdlog::logger* cached = &logger::get(LOGGER_NAME); \
    cached->debug(__VA_ARGS__);                                 \
  } while (0)
#define LOG_INFO(...)                                           \
  do {                                                          \
    static spdlog::logger* cached = &logger::get(LOGGER_NAME); \
    cached->info(__VA_ARGS__);                                  \
  } while (0)
#define LOG_WARN(...)                                           \
  do {                                                          \
    static spdlog::logger* cached = &logger::get(LOGGER_NAME); \
    cached->warn(__VA_ARGS__);                                  \
  } while (0)
#define LOG_ERROR(...)                                          \
  do {                                                          \
    static spdlog::logger* cached = &logger::get(LOGGER_NAME); \
    cached->error(__VA_ARGS__);                                 \
  } while (0)
