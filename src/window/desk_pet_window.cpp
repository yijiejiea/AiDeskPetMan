#include "window/desk_pet_window.hpp"

#include <QCloseEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QVBoxLayout>
#include <Qt>

#include <algorithm>
#include <system_error>
#include <utility>

#include "renderer/live2d_canvas.hpp"
#include "spdlog/spdlog.h"
#include "ui/settings_window.hpp"
#include "ui_desk_pet_window.h"

namespace mikudesk::window {

namespace {

constexpr int kMinModelDimensionPx = 64;
constexpr int kDragClickThresholdPx = 4;

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
  std::error_code error_code;
  const auto weakly_canonical = std::filesystem::weakly_canonical(path, error_code);
  if (!error_code) {
    return weakly_canonical;
  }

  const auto absolute_path = std::filesystem::absolute(path, error_code);
  if (!error_code) {
    return absolute_path.lexically_normal();
  }

  return path.lexically_normal();
}

bool ContainsModel3Json(const std::filesystem::path& directory_path) {
  std::error_code error_code;
  if (!std::filesystem::exists(directory_path, error_code) ||
      !std::filesystem::is_directory(directory_path, error_code)) {
    return false;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory_path, error_code)) {
    if (error_code) {
      return false;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto file_name = entry.path().filename().string();
    if (file_name.ends_with(".model3.json")) {
      return true;
    }
  }

  return false;
}

QRect BuildModelRect(int view_width, int view_height, int model_width_px, int model_height_px) {
  const int safe_view_width = std::max(view_width, 1);
  const int safe_view_height = std::max(view_height, 1);
  const int clamped_width = std::clamp(model_width_px, kMinModelDimensionPx, safe_view_width);
  const int clamped_height =
      std::clamp(model_height_px, kMinModelDimensionPx, safe_view_height);

  const int origin_x = (safe_view_width - clamped_width) / 2;
  const int origin_y = (safe_view_height - clamped_height) / 2;
  return {origin_x, origin_y, clamped_width, clamped_height};
}

}  // namespace

DeskPetWindow::DeskPetWindow(const app::AppConfig& app_config,
                             const std::filesystem::path& config_path,
                             std::function<void()> on_close, QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::DeskPetWindow>()),
      on_close_(std::move(on_close)),
      app_config_(app_config),
      config_path_(config_path),
      skin_config_(app_config.skin),
      live2d_renderer_(std::make_unique<renderer::Live2DRenderer>()) {
  ui_->setupUi(this);

  const app::WindowConfig& window_config = app_config_.window;
  Qt::WindowFlags window_flags = Qt::FramelessWindowHint | Qt::Tool;
  if (window_config.always_on_top) {
    window_flags |= Qt::WindowStaysOnTopHint;
  }

  setWindowFlags(window_flags);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_NoSystemBackground, false);
  setMouseTracking(true);
  setFixedSize(window_config.width, window_config.height);
  setWindowOpacity(window_config.opacity);
  setFocusPolicy(Qt::StrongFocus);

  available_skin_directories_ = DiscoverAvailableSkins();

  auto monitor_result = performance_monitor_.Initialize();
  if (!monitor_result.has_value()) {
    spdlog::warn("Performance monitor initialization failed: {}",
                 app::AppErrorToString(monitor_result.error()));
  }
  performance_monitor_.SetEnabled(app_config_.debug.show_performance_metrics);
  performance_monitor_.SetRefreshIntervalMs(app_config_.debug.metrics_refresh_ms);

  EnsureLive2dViewInitialized();
  settings_window_ = std::make_unique<ui::SettingsWindow>(
      app_config_, config_path_, [this](const app::AppConfig& updated_config) {
        ApplyUpdatedConfig(updated_config);
      },
      [this]() { return performance_monitor_.GetSnapshot(); });

  frame_timer_.start();
}

DeskPetWindow::~DeskPetWindow() = default;

void DeskPetWindow::AdvanceFrame() {
  performance_monitor_.Tick();

  if (!skin_config_.enable_live2d || live2d_renderer_ == nullptr || !live2d_renderer_->IsReady()) {
    return;
  }

  const qint64 elapsed_ms = frame_timer_.restart();
  const double delta_seconds = static_cast<double>(elapsed_ms) / 1000.0;
  UpdatePointerFromCursor();
  live2d_renderer_->Update(delta_seconds);
  live2d_renderer_->TickIdle(delta_seconds);
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->update();
  }
}

void DeskPetWindow::ShowSettingsWindow() {
  if (settings_window_ == nullptr) {
    return;
  }
  settings_window_->RefreshFromConfig(app_config_);
  settings_window_->show();
  settings_window_->raise();
  settings_window_->activateWindow();
}

QRect DeskPetWindow::GetModelRectInView() const {
  return BuildModelRect(ui_->live2d_view->width(), ui_->live2d_view->height(),
                        skin_config_.model_width_px, skin_config_.model_height_px);
}

bool DeskPetWindow::IsPointInsideModelArea(const QPoint& window_point) const {
  if (ui_->live2d_view->width() <= 0 || ui_->live2d_view->height() <= 0) {
    return false;
  }

  const QPoint view_point = ui_->live2d_view->mapFrom(this, window_point);
  return GetModelRectInView().contains(view_point);
}

void DeskPetWindow::UpdatePointerFromCursor() {
  if (live2d_renderer_ == nullptr || !live2d_renderer_->IsReady()) {
    return;
  }
  if (!skin_config_.enable_eye_tracking) {
    live2d_renderer_->SetPointerPosition(0.0F, 0.0F);
    return;
  }
  if (ui_->live2d_view->width() <= 0 || ui_->live2d_view->height() <= 0) {
    return;
  }

  const QPoint local_point = ui_->live2d_view->mapFromGlobal(QCursor::pos());
  const QRect model_rect = GetModelRectInView();
  const float half_width = std::max(model_rect.width() / 2.0F, 1.0F);
  const float half_height = std::max(model_rect.height() / 2.0F, 1.0F);
  const float centered_x =
      static_cast<float>(local_point.x()) - static_cast<float>(model_rect.center().x());
  const float centered_y =
      static_cast<float>(local_point.y()) - static_cast<float>(model_rect.center().y());

  const float normalized_x = std::clamp(centered_x / half_width, -1.0F, 1.0F);
  const float normalized_y = std::clamp(-(centered_y / half_height), -1.0F, 1.0F);
  live2d_renderer_->SetPointerPosition(normalized_x, normalized_y);
}

void DeskPetWindow::ApplyUpdatedConfig(const app::AppConfig& updated_config) {
  const app::AppConfig old_config = app_config_;
  app_config_ = updated_config;
  skin_config_ = app_config_.skin;
  performance_monitor_.SetEnabled(app_config_.debug.show_performance_metrics);
  performance_monitor_.SetRefreshIntervalMs(app_config_.debug.metrics_refresh_ms);

  if (old_config.window.always_on_top != app_config_.window.always_on_top) {
    Qt::WindowFlags flags = windowFlags();
    if (app_config_.window.always_on_top) {
      flags |= Qt::WindowStaysOnTopHint;
    } else {
      flags &= ~Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    show();
  }

  const bool skin_root_changed = old_config.skin.directory != app_config_.skin.directory;
  const bool skin_current_changed = old_config.skin.current != app_config_.skin.current;
  const bool live2d_enabled_changed =
      old_config.skin.enable_live2d != app_config_.skin.enable_live2d;

  if (live2d_enabled_changed || skin_root_changed || skin_current_changed) {
    EnsureLive2dViewInitialized();
  }
  ApplyLive2dBehaviorConfig();
  available_skin_directories_ = DiscoverAvailableSkins();
}

void DeskPetWindow::ApplyLive2dBehaviorConfig() {
  if (live2d_renderer_ == nullptr || !live2d_renderer_->IsReady()) {
    return;
  }
  live2d_renderer_->SetEyeTrackingEnabled(skin_config_.enable_eye_tracking);
  live2d_renderer_->SetIdleAnimationEnabled(skin_config_.enable_idle_animation);
  live2d_renderer_->SetIdleMotionGroup(skin_config_.idle_motion_group);
  live2d_renderer_->SetIdleIntervalSeconds(skin_config_.idle_interval_seconds);
  live2d_renderer_->SetModelRenderSize(skin_config_.model_width_px, skin_config_.model_height_px);
}

void DeskPetWindow::EnsureLive2dViewInitialized() {
  if (live2d_renderer_ == nullptr) {
    return;
  }

  if (!skin_config_.enable_live2d) {
    ui_->live2d_view->setVisible(false);
    spdlog::info("Live2D disabled by config.");
    return;
  }
  ui_->live2d_view->setVisible(true);

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  auto init_result = live2d_renderer_->Initialize(skin_config_);
  if (!init_result.has_value()) {
    spdlog::warn("Live2D initialization failed: {}", app::AppErrorToString(init_result.error()));
    return;
  }

  if (live2d_canvas_ == nullptr) {
    live2d_canvas_ = new renderer::Live2DCanvas(live2d_renderer_.get(), ui_->live2d_view);
    live2d_canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* canvas_layout = new QVBoxLayout(ui_->live2d_view);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    canvas_layout->addWidget(live2d_canvas_);
  }

  ApplyLive2dBehaviorConfig();
  spdlog::info("Live2D view initialized in Qt UI.");
#else
  spdlog::warn("Live2D is enabled in config, but binary was built with MIKUDESK_ENABLE_LIVE2D=OFF.");
#endif
}

std::filesystem::path DeskPetWindow::BuildCurrentModelDirectory() const {
  return skin_config_.directory / skin_config_.current;
}

void DeskPetWindow::ReloadCurrentSkin() {
  if (live2d_renderer_ == nullptr) {
    return;
  }
  const std::filesystem::path model_directory = BuildCurrentModelDirectory();
  auto reload_result = live2d_renderer_->SwitchSkin(model_directory);
  if (!reload_result.has_value()) {
    spdlog::warn("Live2D hot reload failed for {}: {}", model_directory.string(),
                 app::AppErrorToString(reload_result.error()));
    return;
  }
  ApplyLive2dBehaviorConfig();
  spdlog::info("Live2D skin reloaded: {}", model_directory.string());
}

std::vector<std::filesystem::path> DeskPetWindow::DiscoverAvailableSkins() const {
  std::vector<std::filesystem::path> discovered_directories;

  std::error_code error_code;
  const std::filesystem::path skin_root = skin_config_.directory;
  if (!std::filesystem::exists(skin_root, error_code) ||
      !std::filesystem::is_directory(skin_root, error_code)) {
    return discovered_directories;
  }

  std::filesystem::recursive_directory_iterator iterator(
      skin_root, std::filesystem::directory_options::skip_permission_denied, error_code);
  std::filesystem::recursive_directory_iterator end;
  for (; !error_code && iterator != end; iterator.increment(error_code)) {
    if (!iterator->is_directory()) {
      continue;
    }

    if (!ContainsModel3Json(iterator->path())) {
      continue;
    }

    discovered_directories.push_back(NormalizePath(iterator->path()));
  }

  std::sort(discovered_directories.begin(), discovered_directories.end());
  discovered_directories.erase(
      std::unique(discovered_directories.begin(), discovered_directories.end()),
      discovered_directories.end());
  return discovered_directories;
}

void DeskPetWindow::SwitchSkinByOffset(int offset) {
  if (offset == 0 || live2d_renderer_ == nullptr) {
    return;
  }

  available_skin_directories_ = DiscoverAvailableSkins();
  if (available_skin_directories_.empty()) {
    spdlog::warn("No available Live2D skin directory found under {}", skin_config_.directory.string());
    return;
  }

  const std::filesystem::path current_directory = NormalizePath(BuildCurrentModelDirectory());
  auto current_it = std::find(available_skin_directories_.begin(), available_skin_directories_.end(),
                              current_directory);

  std::size_t current_index = 0;
  if (current_it != available_skin_directories_.end()) {
    current_index =
        static_cast<std::size_t>(std::distance(available_skin_directories_.begin(), current_it));
  }

  const int skin_count = static_cast<int>(available_skin_directories_.size());
  const int normalized_offset = offset % skin_count;
  int next_index = static_cast<int>(current_index) + normalized_offset;
  if (next_index < 0) {
    next_index += skin_count;
  }
  next_index %= skin_count;

  const std::filesystem::path target_directory =
      available_skin_directories_[static_cast<std::size_t>(next_index)];
  auto switch_result = live2d_renderer_->SwitchSkin(target_directory);
  if (!switch_result.has_value()) {
    spdlog::warn("Live2D skin switch failed for {}: {}", target_directory.string(),
                 app::AppErrorToString(switch_result.error()));
    return;
  }

  std::error_code error_code;
  const std::filesystem::path normalized_skin_root = NormalizePath(skin_config_.directory);
  const auto relative_path =
      std::filesystem::relative(target_directory, normalized_skin_root, error_code);
  if (!error_code && !relative_path.empty()) {
    skin_config_.current = relative_path.generic_string();
  } else {
    skin_config_.current = target_directory.generic_string();
  }
  app_config_.skin.current = skin_config_.current;
  ApplyLive2dBehaviorConfig();

  spdlog::info("Live2D skin switched to {} (Ctrl+Shift+N/Ctrl+Shift+P)", target_directory.string());
}

void DeskPetWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    press_started_on_model_ = IsPointInsideModelArea(event->position().toPoint());
    if (!press_started_on_model_) {
      QWidget::mousePressEvent(event);
      return;
    }

    dragging_ = true;
    press_moved_ = false;
    drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    press_global_position_ = event->globalPosition().toPoint();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void DeskPetWindow::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    if ((event->globalPosition().toPoint() - press_global_position_).manhattanLength() >
        kDragClickThresholdPx) {
      press_moved_ = true;
    }
    move(event->globalPosition().toPoint() - drag_offset_);
    event->accept();
  }

  UpdatePointerFromCursor();

  QWidget::mouseMoveEvent(event);
}

void DeskPetWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const bool should_trigger_click = press_started_on_model_ && !press_moved_ &&
                                      IsPointInsideModelArea(event->position().toPoint());
    dragging_ = false;
    press_started_on_model_ = false;
    press_moved_ = false;
    if (should_trigger_click && live2d_renderer_ != nullptr) {
      live2d_renderer_->TriggerClickExpression();
    }
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void DeskPetWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    if (on_close_) {
      on_close_();
    }
    close();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_R && event->modifiers().testFlag(Qt::ControlModifier) &&
      event->modifiers().testFlag(Qt::ShiftModifier)) {
    ReloadCurrentSkin();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_N && event->modifiers().testFlag(Qt::ControlModifier) &&
      event->modifiers().testFlag(Qt::ShiftModifier)) {
    SwitchSkinByOffset(1);
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_P && event->modifiers().testFlag(Qt::ControlModifier) &&
      event->modifiers().testFlag(Qt::ShiftModifier)) {
    SwitchSkinByOffset(-1);
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Comma && event->modifiers().testFlag(Qt::ControlModifier)) {
    ShowSettingsWindow();
    event->accept();
    return;
  }

  QWidget::keyPressEvent(event);
}

void DeskPetWindow::closeEvent(QCloseEvent* event) {
  if (settings_window_ != nullptr) {
    settings_window_->hide();
  }
  if (on_close_) {
    on_close_();
  }
  QWidget::closeEvent(event);
}

}  // namespace mikudesk::window
