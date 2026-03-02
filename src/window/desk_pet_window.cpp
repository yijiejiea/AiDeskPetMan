#include "window/desk_pet_window.hpp"

#include <QCloseEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPoint>
#include <QVBoxLayout>
#include <Qt>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "renderer/live2d_canvas.hpp"
#include "spdlog/spdlog.h"
#include "ui/chat_bubble.hpp"
#include "ui/chat_input_dialog.hpp"
#include "ui/settings_window.hpp"
#include "ui_desk_pet_window.h"

namespace mikudesk::window {

namespace {

constexpr int kMinModelDimensionPx = 64;
constexpr int kMaxModelDimensionPx = 4096;
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

std::string FormatPerformanceSnapshot(const diagnostics::PerformanceSnapshot& snapshot) {
  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream << std::setprecision(1);
  stream << "cpu=" << snapshot.cpu_process_percent << "%, gpu=";
  if (snapshot.gpu_available && snapshot.gpu_process_percent >= 0.0) {
    stream << snapshot.gpu_process_percent << "%";
  } else {
    stream << "N/A";
  }
  stream << ", ws=" << snapshot.process_working_set_bytes
         << ", private=" << snapshot.process_private_bytes << ", gpu_mem=";
  if (snapshot.gpu_available) {
    stream << snapshot.process_gpu_dedicated_bytes;
  } else {
    stream << "N/A";
  }
  return stream.str();
}

QString BuildChatFailureMessage(app::AppError error) {
  switch (error) {
    case app::AppError::kLlamaNotEnabled:
      return QStringLiteral(
          "Local model unavailable: this exe was built without llama.cpp support.");
    case app::AppError::kLlamaLoadFailed:
      return QStringLiteral(
          "Local model load failed: please use a GGUF model (.gguf), not safetensors.");
    case app::AppError::kApiKeyMissing:
      return QStringLiteral("Cloud mode API key is missing. Configure it in Settings.");
    case app::AppError::kAiEngineNotReady:
      return QStringLiteral(
          "AI engine not ready. Check current mode and model/API key configuration.");
    default:
      break;
  }

  return QString::fromStdString("AI request failed: " + std::string(app::AppErrorToString(error)));
}

}  // namespace

DeskPetWindow::DeskPetWindow(const app::AppConfig& app_config,
                             const std::filesystem::path& config_path,
                             std::function<void()> on_close, QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::DeskPetWindow>()),
      on_close_(std::move(on_close)),
      app_config_(app_config),
      chat_service_(std::make_unique<ai::ChatService>(app_config_)),
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
  setFocusPolicy(Qt::StrongFocus);
  ApplyWindowGeometryConfig();

  available_skin_directories_ = DiscoverAvailableSkins();

  auto monitor_result = performance_monitor_.Initialize();
  if (!monitor_result.has_value()) {
    spdlog::warn("Performance monitor initialization failed: {}",
                 app::AppErrorToString(monitor_result.error()));
  }
  performance_monitor_.SetEnabled(app_config_.debug.show_performance_metrics);
  performance_monitor_.SetRefreshIntervalMs(app_config_.debug.metrics_refresh_ms);

  EnsureLive2dViewInitialized();
  chat_bubble_ = std::make_unique<ui::ChatBubble>(this);
  chat_bubble_->hide();
  chat_input_dialog_ = std::make_unique<ui::ChatInputDialog>(this);
  chat_input_dialog_->SetSubmitHandler(
      [this](QString text) { SubmitChatMessage(text.toStdString()); });
  settings_window_ = std::make_unique<ui::SettingsWindow>(
      app_config_, config_path_, [this](const app::AppConfig& updated_config) {
        ApplyUpdatedConfig(updated_config);
      },
      [this]() { return performance_monitor_.GetSnapshot(); },
      [this]() { return BuildInferenceBackendStatusText(); });

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
  if (chat_bubble_ != nullptr && chat_bubble_->isVisible()) {
    chat_bubble_->Reposition(GetModelRectInView());
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

void DeskPetWindow::ToggleInferenceMode() {
  if (chat_service_ == nullptr) {
    return;
  }

  const app::InferenceMode current_mode = chat_service_->GetInferenceMode();
  const app::InferenceMode target_mode = current_mode == app::InferenceMode::kTokenApi
                                             ? app::InferenceMode::kLocalModel
                                             : app::InferenceMode::kTokenApi;
  auto switch_result = chat_service_->SetInferenceMode(target_mode);
  if (!switch_result.has_value()) {
    spdlog::warn("Failed to switch inference mode: {}",
                 app::AppErrorToString(switch_result.error()));
    return;
  }

  app_config_.ai.inference_mode = target_mode;
  spdlog::info("Inference mode switched to {}", app::InferenceModeToString(target_mode));

  if (settings_window_ != nullptr && settings_window_->isVisible()) {
    settings_window_->RefreshFromConfig(app_config_);
  }

  if (chat_bubble_ != nullptr) {
    chat_bubble_->SetText(
        QString::fromStdString("Inference mode: " + app::InferenceModeToString(target_mode)),
        false);
    chat_bubble_->Reposition(GetModelRectInView());
  }
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
  if (chat_service_ != nullptr) {
    chat_service_->UpdateConfig(app_config_);
  }
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
  ApplyWindowGeometryConfig();

  const bool skin_root_changed = old_config.skin.directory != app_config_.skin.directory;
  const bool skin_current_changed = old_config.skin.current != app_config_.skin.current;
  const bool live2d_enabled_changed =
      old_config.skin.enable_live2d != app_config_.skin.enable_live2d;

  if (!skin_config_.enable_live2d) {
    EnsureLive2dViewInitialized();
  } else if (live2d_enabled_changed || live2d_renderer_ == nullptr || !live2d_renderer_->IsReady()) {
    EnsureLive2dViewInitialized();
  } else if (skin_root_changed || skin_current_changed) {
    if (live2d_canvas_ != nullptr) {
      live2d_canvas_->makeCurrent();
    }
    auto switch_result = live2d_renderer_->SwitchSkin(BuildCurrentModelDirectory());
    if (live2d_canvas_ != nullptr) {
      live2d_canvas_->doneCurrent();
    }
    if (!switch_result.has_value()) {
      spdlog::warn("Live2D skin switch from settings failed: {}",
                   app::AppErrorToString(switch_result.error()));
    } else if (live2d_canvas_ != nullptr) {
      live2d_canvas_->update();
    }
  }
  ApplyLive2dBehaviorConfig();
  available_skin_directories_ = DiscoverAvailableSkins();
  if (chat_bubble_ != nullptr && chat_bubble_->isVisible()) {
    chat_bubble_->Reposition(GetModelRectInView());
  }
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

void DeskPetWindow::ApplyWindowGeometryConfig() {
  int target_width = std::clamp(app_config_.window.width, kMinModelDimensionPx, kMaxModelDimensionPx);
  int target_height =
      std::clamp(app_config_.window.height, kMinModelDimensionPx, kMaxModelDimensionPx);

  if (app_config_.window.auto_fit_model_rect) {
    const int padding = std::clamp(app_config_.window.min_model_window_padding_px, 0, 64);
    target_width = std::clamp(skin_config_.model_width_px + (padding * 2), kMinModelDimensionPx,
                              kMaxModelDimensionPx);
    target_height = std::clamp(skin_config_.model_height_px + (padding * 2), kMinModelDimensionPx,
                               kMaxModelDimensionPx);
  }

  setFixedSize(target_width, target_height);
  setWindowOpacity(std::clamp(app_config_.window.opacity, 0.1F, 1.0F));
}

void DeskPetWindow::ShowChatInputDialog() {
  if (chat_input_dialog_ == nullptr) {
    return;
  }

  const QRect model_rect = GetModelRectInView();
  const QPoint anchor_local(model_rect.center().x(), model_rect.bottom());
  const QPoint anchor_global = ui_->live2d_view->mapToGlobal(anchor_local);
  chat_input_dialog_->ShowNear(anchor_global);
}

void DeskPetWindow::SubmitChatMessage(std::string user_message) {
  if (chat_service_ == nullptr || chat_bubble_ == nullptr) {
    return;
  }

  if (user_message.empty()) {
    return;
  }

  if (chat_request_in_flight_) {
    UpdateChatBubbleText(QStringLiteral("上一条消息还在处理中，请稍后。"), false);
    return;
  }

  if (chat_worker_.joinable()) {
    chat_worker_.request_stop();
    chat_worker_.join();
  }

  const std::string backend_status_text = BuildInferenceBackendStatusText();
  spdlog::info("Chat request begin: {}", backend_status_text);
  LogInferencePerformanceSnapshot("before_request");

  chat_request_in_flight_ = true;
  UpdateChatBubbleText(QStringLiteral("思考中..."), true);

  chat_worker_ = std::jthread([this, message = std::move(user_message),
                               backend_status_text](std::stop_token stop_token) {
    std::string streamed_reply;
    auto last_stream_ui_update = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
    auto reply_result = chat_service_->SendMessage(
        message,
        [this, &streamed_reply, &last_stream_ui_update](std::string_view chunk) {
          streamed_reply.append(chunk.data(), chunk.size());
          const auto now = std::chrono::steady_clock::now();
          if (now - last_stream_ui_update < std::chrono::milliseconds(50)) {
            return;
          }
          last_stream_ui_update = now;
          const QString stream_text =
              QString::fromUtf8(streamed_reply.data(), static_cast<int>(streamed_reply.size()));
          QMetaObject::invokeMethod(
              this,
              [this, stream_text]() { UpdateChatBubbleText(stream_text, true); },
              Qt::QueuedConnection);
        },
        stop_token);

    if (stop_token.stop_requested()) {
      QMetaObject::invokeMethod(this, [this]() { chat_request_in_flight_ = false; },
                                Qt::QueuedConnection);
      return;
    }

    if (!reply_result.has_value()) {
      const app::AppError error = reply_result.error();
      spdlog::warn("Chat request failed: {}, backend_status={}",
                   app::AppErrorToString(error), backend_status_text);
      LogInferencePerformanceSnapshot("after_request_failed");
      const QString error_text = BuildChatFailureMessage(error);
      QMetaObject::invokeMethod(
          this,
          [this, error_text]() {
            chat_request_in_flight_ = false;
            UpdateChatBubbleText(error_text, false);
          },
          Qt::QueuedConnection);
      return;
    }

    if (streamed_reply.empty()) {
      streamed_reply = reply_result->text;
    }

    spdlog::info("Chat request succeeded: backend_status={}", backend_status_text);
    LogInferencePerformanceSnapshot("after_request_success");

    const QString final_text =
        QString::fromUtf8(streamed_reply.data(), static_cast<int>(streamed_reply.size()));
    QMetaObject::invokeMethod(
        this,
        [this, final_text]() {
          chat_request_in_flight_ = false;
          UpdateChatBubbleText(final_text, false);
        },
        Qt::QueuedConnection);
  });
}

std::string DeskPetWindow::BuildInferenceBackendStatusText() const {
  if (chat_service_ == nullptr) {
    return "mode=none, engine=none";
  }

  const app::InferenceMode mode = chat_service_->GetInferenceMode();
  const std::string_view engine_name = chat_service_->GetCurrentEngineName();
  std::string status = "mode=" + app::InferenceModeToString(mode) +
                       ", engine=" + std::string(engine_name);
  if (mode == app::InferenceMode::kLocalModel) {
    status += ", local_gpu_layers=" + std::to_string(app_config_.ai.local_gpu_layers);
    status += ", local_threads=" + std::to_string(app_config_.ai.local_threads);
    status += ", local_ctx=" + std::to_string(app_config_.ai.local_context_length);
  } else {
    status += ", provider=" + app::AiProviderToString(app_config_.ai.provider);
    status += ", model=" + app_config_.ai.api_model;
  }
  return status;
}

void DeskPetWindow::LogInferencePerformanceSnapshot(std::string_view stage) const {
  const diagnostics::PerformanceSnapshot snapshot = performance_monitor_.GetSnapshot();
  spdlog::info("Inference perf {}: {}", stage, FormatPerformanceSnapshot(snapshot));
}

void DeskPetWindow::UpdateChatBubbleText(const QString& text, bool streaming) {
  if (chat_bubble_ == nullptr) {
    return;
  }

  chat_bubble_->SetText(text, streaming);
  chat_bubble_->Reposition(GetModelRectInView());
  chat_bubble_->update();
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
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->makeCurrent();
  }
  auto init_result = live2d_renderer_->Initialize(skin_config_);
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->doneCurrent();
  }
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
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->makeCurrent();
  }
  auto reload_result = live2d_renderer_->SwitchSkin(model_directory);
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->doneCurrent();
  }
  if (!reload_result.has_value()) {
    spdlog::warn("Live2D hot reload failed for {}: {}", model_directory.string(),
                 app::AppErrorToString(reload_result.error()));
    return;
  }
  ApplyLive2dBehaviorConfig();
  ApplyWindowGeometryConfig();
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->update();
  }
  if (chat_bubble_ != nullptr && chat_bubble_->isVisible()) {
    chat_bubble_->Reposition(GetModelRectInView());
  }
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
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->makeCurrent();
  }
  auto switch_result = live2d_renderer_->SwitchSkin(target_directory);
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->doneCurrent();
  }
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
  ApplyWindowGeometryConfig();
  if (live2d_canvas_ != nullptr) {
    live2d_canvas_->update();
  }
  if (chat_bubble_ != nullptr && chat_bubble_->isVisible()) {
    chat_bubble_->Reposition(GetModelRectInView());
  }

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
  if (chat_bubble_ != nullptr && chat_bubble_->isVisible()) {
    chat_bubble_->Reposition(GetModelRectInView());
  }

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

  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
      event->modifiers().testFlag(Qt::ControlModifier)) {
    ShowChatInputDialog();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Comma && event->modifiers().testFlag(Qt::ControlModifier)) {
    ShowSettingsWindow();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_I && event->modifiers().testFlag(Qt::ControlModifier) &&
      event->modifiers().testFlag(Qt::ShiftModifier)) {
    ToggleInferenceMode();
    event->accept();
    return;
  }

  QWidget::keyPressEvent(event);
}

void DeskPetWindow::closeEvent(QCloseEvent* event) {
  if (chat_worker_.joinable()) {
    chat_worker_.request_stop();
  }
  if (chat_input_dialog_ != nullptr) {
    chat_input_dialog_->hide();
  }
  if (chat_bubble_ != nullptr) {
    chat_bubble_->hide();
  }
  if (settings_window_ != nullptr) {
    settings_window_->hide();
  }
  if (on_close_) {
    on_close_();
  }
  QWidget::closeEvent(event);
}

}  // namespace mikudesk::window
