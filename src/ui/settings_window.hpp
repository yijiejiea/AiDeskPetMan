#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QDialog>
#include <QString>
#include <QTimer>

#include "app/config.hpp"
#include "diagnostics/performance_monitor.hpp"
#include "resource/config_store.hpp"

namespace Ui {
class SettingsWindow;
}

namespace mikudesk::ui {

class SettingsWindow final : public QDialog {
 public:
  explicit SettingsWindow(const app::AppConfig& initial_config,
                          std::filesystem::path config_path,
                          std::function<void(const app::AppConfig&)> on_apply,
                          std::function<diagnostics::PerformanceSnapshot()> get_performance_snapshot,
                          QWidget* parent = nullptr);
  ~SettingsWindow() override;

  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  void RefreshFromConfig(const app::AppConfig& config);

 private:
  void InitializeSignals();
  void PopulateUiFromConfig();
  void ReadUiToConfig();
  void RefreshSkinCandidates();
  void ImportSkinDirectory();
  void UpdateAiControlState() const;
  void UpdatePerformanceSnapshotUi();
  void UpdatePerformanceTimerState();
  static QString FormatBytes(std::uint64_t bytes);
  bool SaveConfigToDisk();
  std::vector<std::filesystem::path> DiscoverSkinDirectories() const;

  std::unique_ptr<Ui::SettingsWindow> ui_;
  app::AppConfig config_;
  std::filesystem::path config_path_;
  resource::ConfigStore config_store_;
  std::function<void(const app::AppConfig&)> on_apply_;
  std::function<diagnostics::PerformanceSnapshot()> get_performance_snapshot_;
  std::vector<std::filesystem::path> available_skin_directories_;
  QTimer performance_timer_;
  bool updating_ui_ = false;
};

}  // namespace mikudesk::ui
