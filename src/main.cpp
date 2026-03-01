#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <QApplication>

#include "app/app_error.hpp"
#include "app/application.hpp"
#include "app/config.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

void PrintUsage() {
  std::cout << "MikuDesk options:\n"
            << "  --debug\n"
            << "Runtime options are loaded from config/config.json:\n"
            << "  crash.dump_type: normal|full\n"
            << "  debug.smoke_test_seconds: N\n"
            << "  debug.crash_test: true|false\n";
}

[[noreturn]] void TriggerCrashTest() {
#if defined(_WIN32)
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
  std::abort();
#else
  std::raise(SIGSEGV);
  std::abort();
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
    std::cerr << "Invalid process arguments." << std::endl;
    return 1;
  }

  QApplication qt_application(argc, argv);

  bool debug_mode = false;
  const std::filesystem::path config_path = std::filesystem::path("config") / "config.json";

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    }
    if (arg == "--debug") {
      debug_mode = true;
      continue;
    }
    std::cerr << "Unknown argument: " << arg
              << ". Runtime behavior is configured in config/config.json." << std::endl;
    PrintUsage();
    return 1;
  }

  mikudesk::app::Application application;
  auto initialize_result = application.Initialize(config_path, debug_mode, std::nullopt);
  if (!initialize_result.has_value()) {
    std::cerr << "Failed to initialize application: "
              << mikudesk::app::AppErrorToString(initialize_result.error()) << std::endl;
    return 1;
  }

  if (application.ShouldTriggerCrashTest()) {
    TriggerCrashTest();
  }

  auto run_result = application.Run(application.GetConfiguredSmokeTestDuration());
  application.Shutdown();
  if (!run_result.has_value()) {
    std::cerr << "Application run failed: " << mikudesk::app::AppErrorToString(run_result.error())
              << std::endl;
    return 1;
  }

  return 0;
}
