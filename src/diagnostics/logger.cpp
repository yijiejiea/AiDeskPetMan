#include "diagnostics/logger.hpp"

#include <filesystem>
#include <memory>
#include <string>

#include "spdlog/async.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace mikudesk::diagnostics {

std::mutex Logger::mutex_;
bool Logger::initialized_ = false;

namespace {

spdlog::level::level_enum ToSpdlogLevel(app::LogLevel level) {
  switch (level) {
    case app::LogLevel::kTrace:
      return spdlog::level::trace;
    case app::LogLevel::kDebug:
      return spdlog::level::debug;
    case app::LogLevel::kInfo:
      return spdlog::level::info;
    case app::LogLevel::kWarn:
      return spdlog::level::warn;
    case app::LogLevel::kError:
      return spdlog::level::err;
    case app::LogLevel::kCritical:
      return spdlog::level::critical;
  }
  return spdlog::level::info;
}

}  // namespace

app::Result<void> Logger::Initialize(const app::LogConfig& config) {
  std::scoped_lock lock(mutex_);
  if (initialized_) {
    return {};
  }

  try {
    std::filesystem::create_directories(config.log_directory);
    const std::filesystem::path log_path = config.log_directory / config.log_file_name;

    // spdlog's async logger requires a global thread pool.
    spdlog::init_thread_pool(8192, 1);

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path.string(), config.max_file_size_mb * 1024 * 1024, config.max_file_count);

    auto logger = std::make_shared<spdlog::async_logger>(
        "mikudesk",
        spdlog::sinks_init_list{console_sink, file_sink},
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);

    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid:%t] %v");
    logger->set_level(config.debug_mode ? spdlog::level::trace : ToSpdlogLevel(config.log_level));

    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::warn);
    initialized_ = true;

    spdlog::info("Logger initialized at {}", log_path.string());
    return {};
  } catch (...) {
    return std::unexpected(app::AppError::kLoggerInitFailed);
  }
}

void Logger::SetLevel(app::LogLevel level) {
  std::scoped_lock lock(mutex_);
  if (!initialized_) {
    return;
  }
  spdlog::set_level(ToSpdlogLevel(level));
}

void Logger::Shutdown() {
  std::scoped_lock lock(mutex_);
  if (!initialized_) {
    return;
  }
  spdlog::shutdown();
  initialized_ = false;
}

}  // namespace mikudesk::diagnostics
