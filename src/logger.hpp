#pragma once

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

// We use the spdlog macros to support source location (file/line) if needed
#define LOG_TRACE(...) logger::get(LOGGER_NAME).trace(__VA_ARGS__)
#define LOG_DEBUG(...) logger::get(LOGGER_NAME).debug(__VA_ARGS__)
#define LOG_INFO(...)  logger::get(LOGGER_NAME).info(__VA_ARGS__)
#define LOG_WARN(...)  logger::get(LOGGER_NAME).warn(__VA_ARGS__)
#define LOG_ERROR(...) logger::get(LOGGER_NAME).error(__VA_ARGS__)
