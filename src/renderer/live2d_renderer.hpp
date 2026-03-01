#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "app/app_error.hpp"
#include "app/config.hpp"
#include "renderer/skin_loader.hpp"

namespace mikudesk::renderer {

class Live2DRenderer {
 public:
  Live2DRenderer();
  ~Live2DRenderer();

  app::Result<void> Initialize(const app::SkinConfig& skin_config);
  app::Result<void> SwitchSkin(const std::filesystem::path& model_directory);

  void Update(double delta_seconds);
  void Render();
  void SetPointerPosition(float normalized_x, float normalized_y);
  void TriggerClickExpression();
  void TickIdle(double delta_seconds);
  void SetEyeTrackingEnabled(bool enabled);
  void SetIdleAnimationEnabled(bool enabled);
  void SetIdleMotionGroup(std::string group_name);
  void SetIdleIntervalSeconds(int interval_seconds);
  void SetModelRenderSize(int model_width_px, int model_height_px);

  bool IsReady() const;
  std::vector<std::string> ListExpressions() const;
  std::vector<std::string> ListMotions() const;
  std::string GetActiveExpression() const;
  std::string GetActiveMotion() const;
  std::filesystem::path GetCurrentModelDirectory() const;

 private:
  struct RuntimeState;

  app::Result<void> LoadRuntimeModel(const std::filesystem::path& model_directory);

  app::SkinConfig skin_config_;
  std::unique_ptr<ISkinLoader> skin_loader_;
  std::unique_ptr<RuntimeState> runtime_state_;
  bool ready_ = false;
  double idle_elapsed_seconds_ = 0.0;
  float pointer_x_ = 0.0F;
  float pointer_y_ = 0.0F;
  float eye_ball_x_ = 0.0F;
  float eye_ball_y_ = 0.0F;
  float angle_x_ = 0.0F;
  float angle_y_ = 0.0F;
  std::size_t next_expression_index_ = 0;
  bool expression_unavailable_logged_ = false;
  bool render_failure_logged_ = false;
  std::chrono::steady_clock::time_point last_pointer_log_time_{};
  float last_logged_pointer_x_ = 0.0F;
  float last_logged_pointer_y_ = 0.0F;
  bool pointer_log_initialized_ = false;
  bool eye_tracking_enabled_ = true;
  bool idle_animation_enabled_ = true;
  std::mt19937 random_engine_{std::random_device{}()};
};

}  // namespace mikudesk::renderer
