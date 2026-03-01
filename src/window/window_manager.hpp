#pragma once

#include <filesystem>
#include <memory>

#include "app/app_error.hpp"
#include "app/config.hpp"

class QMenu;
class QSystemTrayIcon;

namespace mikudesk::window {

class DeskPetWindow;

class WindowManager {
 public:
  WindowManager();
  ~WindowManager();

  WindowManager(const WindowManager&) = delete;
  WindowManager& operator=(const WindowManager&) = delete;

  app::Result<void> Create(const app::AppConfig& config, const std::filesystem::path& config_path);
  void PollEvents();
  void BeginFrame();
  void EndFrame();
  bool IsRunning() const;

 private:
  void EnsureTrayIcon();
  void Destroy();

  std::unique_ptr<DeskPetWindow> window_;
  std::unique_ptr<QSystemTrayIcon> tray_icon_;
  std::unique_ptr<QMenu> tray_menu_;
  bool running_ = false;
};

}  // namespace mikudesk::window
