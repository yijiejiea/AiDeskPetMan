#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QCloseEvent>
#include <QPoint>
#include <QRect>
#include <QElapsedTimer>
#include <QWidget>

#include "app/config.hpp"
#include "diagnostics/performance_monitor.hpp"
#include "renderer/live2d_renderer.hpp"

namespace Ui {
class DeskPetWindow;
}

namespace mikudesk::renderer {
class Live2DCanvas;
}

namespace mikudesk::ui {
class SettingsWindow;
}

namespace mikudesk::window {

class DeskPetWindow final : public QWidget {
 public:
  explicit DeskPetWindow(const app::AppConfig& app_config, const std::filesystem::path& config_path,
                         std::function<void()> on_close,
                         QWidget* parent = nullptr);
  ~DeskPetWindow() override;

  DeskPetWindow(const DeskPetWindow&) = delete;
  DeskPetWindow& operator=(const DeskPetWindow&) = delete;

  void AdvanceFrame();
  void ShowSettingsWindow();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
 void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  QRect GetModelRectInView() const;
  bool IsPointInsideModelArea(const QPoint& window_point) const;
  void UpdatePointerFromCursor();
  void ApplyUpdatedConfig(const app::AppConfig& updated_config);
  void ApplyLive2dBehaviorConfig();
  void EnsureLive2dViewInitialized();
  std::filesystem::path BuildCurrentModelDirectory() const;
  void ReloadCurrentSkin();
  std::vector<std::filesystem::path> DiscoverAvailableSkins() const;
  void SwitchSkinByOffset(int offset);

  std::unique_ptr<Ui::DeskPetWindow> ui_;
  std::function<void()> on_close_;
  app::AppConfig app_config_;
  std::filesystem::path config_path_;
  app::SkinConfig skin_config_;
  std::vector<std::filesystem::path> available_skin_directories_;
  std::unique_ptr<ui::SettingsWindow> settings_window_;
  std::unique_ptr<renderer::Live2DRenderer> live2d_renderer_;
  diagnostics::PerformanceMonitor performance_monitor_;
  renderer::Live2DCanvas* live2d_canvas_ = nullptr;
  bool dragging_ = false;
  bool press_started_on_model_ = false;
  bool press_moved_ = false;
  QPoint drag_offset_;
  QPoint press_global_position_;
  QElapsedTimer frame_timer_;
};

}  // namespace mikudesk::window
