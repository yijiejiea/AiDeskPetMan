#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_error.hpp"

namespace mikudesk::renderer {

class ISkinLoader {
 public:
  virtual ~ISkinLoader() = default;

  virtual app::Result<void> Load(const std::filesystem::path& model_directory) = 0;
  virtual std::vector<std::string> ListExpressions() const = 0;
  virtual std::vector<std::string> ListMotions() const = 0;
  virtual app::Result<void> SetExpression(std::string_view name) = 0;
  virtual app::Result<void> StartMotion(std::string_view group, int index) = 0;
};

}  // namespace mikudesk::renderer
