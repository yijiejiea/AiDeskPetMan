#pragma once

#include <chrono>
#include <filesystem>
#include <optional>

#include "app/app_error.hpp"
#include "app/config.hpp"
#include "core/scheduler.hpp"
#include "resource/config_store.hpp"
#include "window/window_manager.hpp"

namespace mikudesk::app {

class Application {
 public:
  Result<void> Initialize(const std::filesystem::path& config_path, bool debug_mode,
                          std::optional<DumpType> dump_type_override);
  Result<void> Run(std::optional<std::chrono::seconds> smoke_test_duration);
  std::optional<std::chrono::seconds> GetConfiguredSmokeTestDuration() const;
  bool ShouldTriggerCrashTest() const;
  void Shutdown();

 private:
  AppConfig config_;
  resource::ConfigStore config_store_;
  core::Scheduler scheduler_;
  window::WindowManager window_manager_;
  bool initialized_ = false;
  bool window_enabled_ = false;
};

}  // namespace mikudesk::app
