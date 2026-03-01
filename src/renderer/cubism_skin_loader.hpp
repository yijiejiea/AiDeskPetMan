#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "renderer/skin_loader.hpp"

namespace mikudesk::renderer {

class CubismSkinLoader final : public ISkinLoader {
 public:
  app::Result<void> Load(const std::filesystem::path& model_directory) override;
  std::vector<std::string> ListExpressions() const override;
  std::vector<std::string> ListMotions() const override;
  std::vector<int> ListMotionIndices(std::string_view group) const;
  app::Result<void> SetExpression(std::string_view name) override;
  app::Result<void> StartMotion(std::string_view group, int index) override;

  const std::filesystem::path& GetModelDirectory() const;
  const std::filesystem::path& GetModelJsonPath() const;
  std::string GetActiveExpression() const;
  std::string GetActiveMotion() const;

 private:
  std::filesystem::path model_directory_;
  std::filesystem::path model_json_path_;
  std::vector<std::string> expressions_;
  std::unordered_map<std::string, std::vector<int>> motions_by_group_;
  std::string active_expression_;
  std::string active_motion_;
};

}  // namespace mikudesk::renderer
