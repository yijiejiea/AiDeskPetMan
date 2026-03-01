#pragma once

#include <mutex>

#include "app/app_error.hpp"
#include "app/config.hpp"

namespace mikudesk::diagnostics {

class Logger {
 public:
  static app::Result<void> Initialize(const app::LogConfig& config);
  static void SetLevel(app::LogLevel level);
  static void Shutdown();

 private:
  static std::mutex mutex_;
  static bool initialized_;
};

}  // namespace mikudesk::diagnostics
