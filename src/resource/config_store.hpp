#pragma once

#include <filesystem>

#include "app/app_error.hpp"
#include "app/config.hpp"

namespace mikudesk::resource {

class ConfigStore {
 public:
  app::Result<app::AppConfig> LoadOrDefault(const std::filesystem::path& config_path) const;
  app::Result<void> Save(const std::filesystem::path& config_path,
                         const app::AppConfig& config) const;
};

}  // namespace mikudesk::resource
