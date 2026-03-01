#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "app/application.hpp"
#include "resource/config_store.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

int main() {
  char* run_integration = nullptr;
  std::size_t env_length = 0;
#if defined(_WIN32)
  _dupenv_s(&run_integration, &env_length, "MIKUDESK_RUN_CRASH_INTEGRATION");
#else
  run_integration = std::getenv("MIKUDESK_RUN_CRASH_INTEGRATION");
#endif
  const bool should_run = run_integration != nullptr && std::string_view(run_integration) == "1";
#if defined(_WIN32)
  free(run_integration);
#endif

  if (!should_run) {
    std::cout << "Skipping crash integration test. "
                 "Set MIKUDESK_RUN_CRASH_INTEGRATION=1 to execute."
              << std::endl;
    return 0;
  }

#if !defined(_WIN32)
  std::cout << "Crash integration test is only supported on Windows." << std::endl;
  return 0;
#else
  namespace fs = std::filesystem;
  const auto id = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path root = fs::temp_directory_path() / ("mikudesk_crash_test_" + id);
  const fs::path config_path = root / "config.json";
  fs::create_directories(root);

  mikudesk::app::AppConfig config;
  config.window.enable_window = false;
  config.log.log_directory = root / "logs";
  config.crash.dump_directory = root / "logs";
  config.crash.enable_handler = true;
  config.crash.write_minidump = true;

  mikudesk::resource::ConfigStore config_store;
  auto save_result = config_store.Save(config_path, config);
  if (!save_result.has_value()) {
    std::cerr << "Failed to write integration config." << std::endl;
    return 1;
  }

  mikudesk::app::Application application;
  auto init_result = application.Initialize(config_path, true, mikudesk::app::DumpType::kNormal);
  if (!init_result.has_value()) {
    std::cerr << "Failed to initialize application for crash integration." << std::endl;
    return 1;
  }

  // Intentional crash path for manual/integration validation.
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
  return 1;
#endif
}
