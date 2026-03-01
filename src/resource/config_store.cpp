#include "resource/config_store.hpp"

#include <fstream>

#include "spdlog/spdlog.h"

namespace mikudesk::resource {

app::Result<app::AppConfig> ConfigStore::LoadOrDefault(
    const std::filesystem::path& config_path) const {
  app::AppConfig default_config;
  if (!std::filesystem::exists(config_path)) {
    spdlog::warn("Config file not found. Using default config: {}", config_path.string());
    return default_config;
  }

  std::ifstream input(config_path);
  if (!input.is_open()) {
    return std::unexpected(app::AppError::kIoReadFailed);
  }

  try {
    nlohmann::json json;
    input >> json;
    app::AppConfig config = json.get<app::AppConfig>();
    return config;
  } catch (...) {
    spdlog::warn("Config file is invalid. Falling back to defaults: {}", config_path.string());
    return default_config;
  }
}

app::Result<void> ConfigStore::Save(const std::filesystem::path& config_path,
                                    const app::AppConfig& config) const {
  if (config_path.has_parent_path()) {
    std::error_code error_code;
    std::filesystem::create_directories(config_path.parent_path(), error_code);
    if (error_code) {
      return std::unexpected(app::AppError::kIoWriteFailed);
    }
  }

  std::ofstream output(config_path, std::ios::trunc);
  if (!output.is_open()) {
    return std::unexpected(app::AppError::kIoWriteFailed);
  }

  nlohmann::json json = config;
  output << json.dump(2) << std::endl;
  if (!output.good()) {
    return std::unexpected(app::AppError::kIoWriteFailed);
  }

  return {};
}

}  // namespace mikudesk::resource
