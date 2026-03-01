#include "renderer/cubism_skin_loader.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace mikudesk::renderer {

namespace {

std::filesystem::path FindModel3Json(const std::filesystem::path& model_directory) {
    if (!std::filesystem::exists(model_directory) || !std::filesystem::is_directory(model_directory)) {
    return {};
  }

  for (const auto& entry : std::filesystem::directory_iterator(model_directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension().string();
    if (extension == ".json" && entry.path().filename().string().find(".model3.") != std::string::npos) {
      return entry.path();
    }
  }
  return {};
}

}  // namespace

app::Result<void> CubismSkinLoader::Load(const std::filesystem::path& model_directory) {
  model_directory_ = model_directory;
  model_json_path_ = FindModel3Json(model_directory);
  expressions_.clear();
  motions_by_group_.clear();
  active_expression_.clear();
  active_motion_.clear();

  if (model_json_path_.empty()) {
    return std::unexpected(app::AppError::kLive2dModelNotFound);
  }

  std::ifstream model_stream(model_json_path_);
  if (!model_stream.is_open()) {
    return std::unexpected(app::AppError::kIoReadFailed);
  }

  nlohmann::json model_json;
  try {
    model_stream >> model_json;
  } catch (...) {
    return std::unexpected(app::AppError::kJsonParseFailed);
  }

  const auto file_references_it = model_json.find("FileReferences");
  if (file_references_it == model_json.end() || !file_references_it->is_object()) {
    return std::unexpected(app::AppError::kLive2dLoadFailed);
  }

  const auto expressions_it = file_references_it->find("Expressions");
  if (expressions_it != file_references_it->end() && expressions_it->is_array()) {
    for (const auto& expression : *expressions_it) {
      const std::string name = expression.value("Name", "");
      if (!name.empty()) {
        expressions_.push_back(name);
      }
    }
  }

  const auto motions_it = file_references_it->find("Motions");
  if (motions_it != file_references_it->end() && motions_it->is_object()) {
    for (const auto& [group_name, motions] : motions_it->items()) {
      if (!motions.is_array()) {
        continue;
      }
      std::vector<int> indices;
      indices.reserve(motions.size());
      for (std::size_t motion_index = 0; motion_index < motions.size(); ++motion_index) {
        indices.push_back(static_cast<int>(motion_index));
      }
      if (!indices.empty()) {
        motions_by_group_.insert_or_assign(group_name, std::move(indices));
      }
    }
  }

  if (!expressions_.empty()) {
    active_expression_ = expressions_.front();
  }
  if (!motions_by_group_.empty()) {
    const auto& first_group = motions_by_group_.begin()->first;
    active_motion_ = first_group + ":0";
  }

  spdlog::info("Loaded Cubism model metadata: {}", model_json_path_.string());
  return {};
}

std::vector<std::string> CubismSkinLoader::ListExpressions() const {
  return expressions_;
}

std::vector<std::string> CubismSkinLoader::ListMotions() const {
  std::vector<std::string> groups;
  groups.reserve(motions_by_group_.size());
  for (const auto& [group_name, _] : motions_by_group_) {
    groups.push_back(group_name);
  }
  return groups;
}

std::vector<int> CubismSkinLoader::ListMotionIndices(std::string_view group) const {
  const auto it = motions_by_group_.find(std::string(group));
  if (it == motions_by_group_.end()) {
    return {};
  }
  return it->second;
}

app::Result<void> CubismSkinLoader::SetExpression(std::string_view name) {
  for (const std::string& expression : expressions_) {
    if (expression == name) {
      active_expression_ = expression;
      spdlog::info("Set Live2D expression: {}", active_expression_);
      return {};
    }
  }
  return std::unexpected(app::AppError::kInvalidArgument);
}

app::Result<void> CubismSkinLoader::StartMotion(std::string_view group, int index) {
  const auto it = motions_by_group_.find(std::string(group));
  if (it == motions_by_group_.end() || index < 0) {
    return std::unexpected(app::AppError::kInvalidArgument);
  }
  const auto& indices = it->second;
  const bool found = std::find(indices.begin(), indices.end(), index) != indices.end();
  if (!found) {
    return std::unexpected(app::AppError::kInvalidArgument);
  }
  active_motion_ = std::string(group) + ":" + std::to_string(index);
  spdlog::info("Start Live2D motion: {}", active_motion_);
  return {};
}

const std::filesystem::path& CubismSkinLoader::GetModelDirectory() const {
  return model_directory_;
}

const std::filesystem::path& CubismSkinLoader::GetModelJsonPath() const {
  return model_json_path_;
}

std::string CubismSkinLoader::GetActiveExpression() const {
  return active_expression_;
}

std::string CubismSkinLoader::GetActiveMotion() const {
  return active_motion_;
}

}  // namespace mikudesk::renderer
