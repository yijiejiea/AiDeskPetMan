#include "app/application.hpp"

#include <algorithm>
#include <thread>

#include <QCoreApplication>

#include "diagnostics/crash_handler.hpp"
#include "diagnostics/logger.hpp"
#include "spdlog/spdlog.h"

namespace mikudesk::app {

#if !defined(MIKUDESK_ENABLE_LIVE2D)
#define MIKUDESK_ENABLE_LIVE2D 0
#endif

#if !defined(MIKUDESK_ENABLE_LLAMA)
#define MIKUDESK_ENABLE_LLAMA 0
#endif

Result<void> Application::Initialize(const std::filesystem::path& config_path, bool debug_mode,
                                     std::optional<DumpType> dump_type_override) {
  if (initialized_) {
    return {};
  }

  std::error_code error_code;
  const bool config_existed = std::filesystem::exists(config_path, error_code);
  if (error_code) {
    return std::unexpected(AppError::kIoReadFailed);
  }

  auto config_result = config_store_.LoadOrDefault(config_path);
  if (!config_result.has_value()) {
    return std::unexpected(config_result.error());
  }

  config_ = *config_result;

  if (debug_mode) {
    config_.debug.enabled = true;
    config_.log.debug_mode = true;
    config_.log.log_level = LogLevel::kTrace;
  }

  if (dump_type_override.has_value()) {
    config_.crash.dump_type = *dump_type_override;
  }

  if (!config_existed) {
    auto save_default_result = config_store_.Save(config_path, config_);
    if (!save_default_result.has_value()) {
      return std::unexpected(save_default_result.error());
    }
  }

  auto logger_result = diagnostics::Logger::Initialize(config_.log);
  if (!logger_result.has_value()) {
    return std::unexpected(logger_result.error());
  }

  spdlog::info("Build feature flags: Live2D={}, Llama={}",
               MIKUDESK_ENABLE_LIVE2D ? "ON" : "OFF",
               MIKUDESK_ENABLE_LLAMA ? "ON" : "OFF");

  auto crash_result = diagnostics::CrashHandler::Install(config_.crash);
  if (!crash_result.has_value()) {
    diagnostics::Logger::Shutdown();
    return std::unexpected(crash_result.error());
  }

  window_enabled_ = config_.window.enable_window;
  if (window_enabled_) {
    auto window_result = window_manager_.Create(config_, config_path);
    if (!window_result.has_value()) {
      diagnostics::CrashHandler::Uninstall();
      diagnostics::Logger::Shutdown();
      return std::unexpected(window_result.error());
    }
  }

  initialized_ = true;
  return {};
}

Result<void> Application::Run(std::optional<std::chrono::seconds> smoke_test_duration) {
  if (!initialized_) {
    return std::unexpected(AppError::kInvalidArgument);
  }

  if (!window_enabled_ && !smoke_test_duration.has_value()) {
    return std::unexpected(AppError::kInvalidArgument);
  }

  const auto begin_time = std::chrono::steady_clock::now();
  const int target_fps = std::max(config_.window.idle_fps, 1);
  const auto frame_time_budget = std::chrono::milliseconds(1000 / target_fps);

  spdlog::info("Application main loop started. Press Esc or close the window to exit.");

  while (!scheduler_.IsStopRequested()) {
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents();
    }

    if (window_enabled_) {
      window_manager_.PollEvents();
      if (!window_manager_.IsRunning()) {
        break;
      }
      window_manager_.BeginFrame();
    }

    const auto frame_begin = std::chrono::steady_clock::now();
    scheduler_.Tick();

    if (window_enabled_) {
      window_manager_.EndFrame();
    }

    if (smoke_test_duration.has_value()) {
      const auto elapsed = std::chrono::steady_clock::now() - begin_time;
      if (elapsed >= *smoke_test_duration) {
        break;
      }
    }

    const auto frame_elapsed = std::chrono::steady_clock::now() - frame_begin;
    if (frame_elapsed < frame_time_budget) {
      std::this_thread::sleep_for(frame_time_budget - frame_elapsed);
    }
  }

  return {};
}

std::optional<std::chrono::seconds> Application::GetConfiguredSmokeTestDuration() const {
  if (!initialized_) {
    return std::nullopt;
  }
  if (config_.debug.smoke_test_seconds <= 0) {
    return std::nullopt;
  }
  return std::chrono::seconds(config_.debug.smoke_test_seconds);
}

bool Application::ShouldTriggerCrashTest() const {
  if (!initialized_) {
    return false;
  }
  return config_.debug.crash_test;
}

void Application::Shutdown() {
  if (!initialized_) {
    return;
  }

  scheduler_.RequestStop();
  diagnostics::CrashHandler::Uninstall();
  diagnostics::Logger::Shutdown();
  initialized_ = false;
}

}  // namespace mikudesk::app
