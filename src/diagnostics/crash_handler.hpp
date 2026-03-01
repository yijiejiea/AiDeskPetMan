#pragma once

#include "app/app_error.hpp"
#include "app/config.hpp"

namespace mikudesk::diagnostics {

class CrashHandler {
 public:
  static app::Result<void> Install(const app::CrashConfig& config);
  static void Uninstall();

 private:
  CrashHandler() = delete;
};

}  // namespace mikudesk::diagnostics
