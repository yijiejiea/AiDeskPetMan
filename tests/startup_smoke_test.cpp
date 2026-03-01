#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "app/application.hpp"
#include "resource/config_store.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

std::filesystem::path BuildTestRoot() {
  const auto id = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("mikudesk_smoke_test_" + id);
}

}  // namespace

int main() {
  namespace fs = std::filesystem;

  const fs::path root = BuildTestRoot();
  const fs::path config_path = root / "config.json";
  fs::create_directories(root);

  mikudesk::app::AppConfig config;
  config.window.enable_window = false;
  config.log.log_directory = root / "logs";
  config.crash.dump_directory = root / "logs";

  mikudesk::resource::ConfigStore config_store;
  auto save_result = config_store.Save(config_path, config);
  if (!Expect(save_result.has_value(), "failed to save smoke config")) {
    return 1;
  }

  mikudesk::app::Application application;
  auto init_result = application.Initialize(config_path, true, std::nullopt);
  if (!Expect(init_result.has_value(), "application initialize failed")) {
    return 1;
  }

  auto run_result = application.Run(std::chrono::seconds(1));
  application.Shutdown();
  if (!Expect(run_result.has_value(), "application run failed")) {
    return 1;
  }

  std::error_code error_code;
  fs::remove_all(root, error_code);
  return 0;
}
