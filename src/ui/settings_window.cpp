#include "ui/settings_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVariant>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <system_error>

#include "security/dpapi_secret_store.hpp"
#include "spdlog/spdlog.h"
#include "ui_settings_window.h"

namespace mikudesk::ui {

namespace {

constexpr int kMinMetricsRefreshMs = 250;
constexpr int kMaxMetricsRefreshMs = 10000;
constexpr int kMinModelDimensionPx = 64;
constexpr int kMaxModelDimensionPx = 4096;
constexpr int kMinWindowPaddingPx = 0;
constexpr int kMaxWindowPaddingPx = 64;

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

bool ContainsModel3JsonInDirectory(const std::filesystem::path& directory_path) {
  std::error_code error_code;
  if (!std::filesystem::exists(directory_path, error_code) ||
      !std::filesystem::is_directory(directory_path, error_code)) {
    return false;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory_path, error_code)) {
    if (error_code) {
      return false;
    }
    if (!entry.is_regular_file(error_code)) {
      continue;
    }
    if (entry.path().filename().string().ends_with(".model3.json")) {
      return true;
    }
  }
  return false;
}

bool ContainsModel3JsonRecursively(const std::filesystem::path& directory_path) {
  std::error_code error_code;
  if (!std::filesystem::exists(directory_path, error_code) ||
      !std::filesystem::is_directory(directory_path, error_code)) {
    return false;
  }

  std::filesystem::recursive_directory_iterator iterator(
      directory_path, std::filesystem::directory_options::skip_permission_denied, error_code);
  std::filesystem::recursive_directory_iterator end;
  for (; !error_code && iterator != end; iterator.increment(error_code)) {
    if (!iterator->is_regular_file(error_code)) {
      continue;
    }
    if (iterator->path().filename().string().ends_with(".model3.json")) {
      return true;
    }
  }
  return false;
}

app::InferenceMode InferenceModeFromComboIndex(int combo_index) {
  if (combo_index == 1) {
    return app::InferenceMode::kLocalModel;
  }
  return app::InferenceMode::kTokenApi;
}

int ComboIndexFromInferenceMode(app::InferenceMode mode) {
  if (mode == app::InferenceMode::kLocalModel) {
    return 1;
  }
  return 0;
}

app::AiProvider AiProviderFromComboIndex(int combo_index) {
  if (combo_index == 1) {
    return app::AiProvider::kDeepSeek;
  }
  if (combo_index == 2) {
    return app::AiProvider::kCustom;
  }
  return app::AiProvider::kOpenAi;
}

int ComboIndexFromAiProvider(app::AiProvider provider) {
  if (provider == app::AiProvider::kDeepSeek) {
    return 1;
  }
  if (provider == app::AiProvider::kCustom) {
    return 2;
  }
  return 0;
}

std::string DefaultApiBaseForProvider(app::AiProvider provider) {
  if (provider == app::AiProvider::kDeepSeek) {
    return "https://api.deepseek.com/v1";
  }
  if (provider == app::AiProvider::kCustom) {
    return "";
  }
  return "https://api.openai.com/v1";
}

}  // namespace

SettingsWindow::SettingsWindow(
    const app::AppConfig& initial_config, std::filesystem::path config_path,
    std::function<void(const app::AppConfig&)> on_apply,
    std::function<diagnostics::PerformanceSnapshot()> get_performance_snapshot, QWidget* parent)
    : QDialog(parent),
      ui_(std::make_unique<Ui::SettingsWindow>()),
      config_(initial_config),
      config_path_(std::move(config_path)),
      on_apply_(std::move(on_apply)),
      get_performance_snapshot_(std::move(get_performance_snapshot)) {
  ui_->setupUi(this);
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);

  performance_timer_.setInterval(
      std::clamp(config_.debug.metrics_refresh_ms, kMinMetricsRefreshMs, kMaxMetricsRefreshMs));
  connect(&performance_timer_, &QTimer::timeout, this, [this]() { UpdatePerformanceSnapshotUi(); });

  InitializeSignals();
  PopulateUiFromConfig();
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::RefreshFromConfig(const app::AppConfig& config) {
  config_ = config;
  PopulateUiFromConfig();
}

void SettingsWindow::InitializeSignals() {
  connect(ui_->apply_button, &QPushButton::clicked, this, [this]() {
    ReadUiToConfig();
    if (on_apply_) {
      on_apply_(config_);
    }
    ui_->status_label->setText(QStringLiteral("已应用到运行时（未落盘）"));
    UpdatePerformanceTimerState();
  });

  connect(ui_->save_button, &QPushButton::clicked, this, [this]() {
    ReadUiToConfig();
    if (on_apply_) {
      on_apply_(config_);
    }
    if (SaveConfigToDisk()) {
      ui_->status_label->setText(QStringLiteral("配置已保存到 config/config.json"));
    }
    UpdatePerformanceTimerState();
  });

  connect(ui_->close_button, &QPushButton::clicked, this, [this]() { close(); });

  connect(ui_->refresh_skins_button, &QPushButton::clicked, this,
          [this]() { RefreshSkinCandidates(); });

  connect(ui_->browse_skin_root_button, &QPushButton::clicked, this, [this]() {
    const QString selected_directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择皮套根目录"),
        QString::fromStdString(config_.skin.directory.generic_string()));
    if (selected_directory.isEmpty()) {
      return;
    }
    ui_->skin_root_line_edit->setText(selected_directory);
    RefreshSkinCandidates();
  });

  connect(ui_->import_skin_button, &QPushButton::clicked, this,
          [this]() { ImportSkinDirectory(); });

  connect(ui_->browse_local_model_button, &QPushButton::clicked, this, [this]() {
    const QString selected_file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择本地推理模型"),
        QString::fromStdString(config_.ai.local_model_path.generic_string()),
        QStringLiteral("Model Files (*.gguf *.bin);;All Files (*)"));
    if (selected_file.isEmpty()) {
      return;
    }
    ui_->local_model_path_line_edit->setText(selected_file);
  });

  connect(ui_->inference_mode_combo_box, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int /*index*/) {
            if (updating_ui_) {
              return;
            }
            UpdateAiControlState();
          });

  connect(ui_->provider_combo_box, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int combo_index) {
            if (updating_ui_) {
              return;
            }
            const app::AiProvider provider = AiProviderFromComboIndex(combo_index);
            if (provider == app::AiProvider::kCustom) {
              return;
            }
            if (ui_->api_base_url_line_edit->text().trimmed().isEmpty()) {
              ui_->api_base_url_line_edit->setText(
                  QString::fromStdString(DefaultApiBaseForProvider(provider)));
            }
          });

  connect(ui_->show_performance_metrics_check_box, &QCheckBox::toggled, this,
          [this](bool /*checked*/) {
            if (updating_ui_) {
              return;
            }
            UpdatePerformanceTimerState();
          });

  connect(ui_->metrics_refresh_ms_spin_box, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int value) {
            if (updating_ui_) {
              return;
            }
            const int clamped_value = std::clamp(value, kMinMetricsRefreshMs, kMaxMetricsRefreshMs);
            performance_timer_.setInterval(clamped_value);
            if (performance_timer_.isActive()) {
              performance_timer_.start();
            }
          });

  connect(ui_->auto_fit_model_rect_check_box, &QCheckBox::toggled, this, [this](bool checked) {
    if (updating_ui_) {
      return;
    }
    ui_->model_window_padding_spin_box->setEnabled(checked);
  });
}

void SettingsWindow::PopulateUiFromConfig() {
  updating_ui_ = true;

  ui_->live2d_enabled_check_box->setChecked(config_.skin.enable_live2d);
  ui_->eye_tracking_enabled_check_box->setChecked(config_.skin.enable_eye_tracking);
  ui_->idle_animation_enabled_check_box->setChecked(config_.skin.enable_idle_animation);
  ui_->idle_interval_spin_box->setValue(std::max(config_.skin.idle_interval_seconds, 1));
  ui_->idle_group_line_edit->setText(QString::fromStdString(config_.skin.idle_motion_group));
  ui_->model_width_spin_box->setValue(
      std::clamp(config_.skin.model_width_px, kMinModelDimensionPx, kMaxModelDimensionPx));
  ui_->model_height_spin_box->setValue(
      std::clamp(config_.skin.model_height_px, kMinModelDimensionPx, kMaxModelDimensionPx));
  ui_->always_on_top_check_box->setChecked(config_.window.always_on_top);
  ui_->auto_fit_model_rect_check_box->setChecked(config_.window.auto_fit_model_rect);
  ui_->model_window_padding_spin_box->setValue(
      std::clamp(config_.window.min_model_window_padding_px, kMinWindowPaddingPx, kMaxWindowPaddingPx));
  ui_->model_window_padding_spin_box->setEnabled(config_.window.auto_fit_model_rect);

  ui_->skin_root_line_edit->setText(QString::fromStdString(config_.skin.directory.generic_string()));
  RefreshSkinCandidates();

  ui_->inference_mode_combo_box->setCurrentIndex(ComboIndexFromInferenceMode(config_.ai.inference_mode));
  ui_->provider_combo_box->setCurrentIndex(ComboIndexFromAiProvider(config_.ai.provider));
  ui_->stream_enabled_check_box->setChecked(config_.ai.stream);
  ui_->request_timeout_ms_spin_box->setValue(config_.ai.request_timeout_ms);
  ui_->max_tokens_spin_box->setValue(config_.ai.max_tokens);
  ui_->temperature_double_spin_box->setValue(config_.ai.temperature);
  ui_->top_p_double_spin_box->setValue(config_.ai.top_p);
  ui_->api_base_url_line_edit->setText(QString::fromStdString(config_.ai.api_base_url));
  ui_->api_model_line_edit->setText(QString::fromStdString(config_.ai.api_model));
  ui_->local_model_path_line_edit->setText(
      QString::fromStdString(config_.ai.local_model_path.generic_string()));
  ui_->local_gpu_layers_spin_box->setValue(config_.ai.local_gpu_layers);
  ui_->local_threads_spin_box->setValue(config_.ai.local_threads);
  ui_->local_context_length_spin_box->setValue(config_.ai.local_context_length);
  ui_->context_rounds_spin_box->setValue(config_.ai.context_rounds);
  ui_->system_prompt_plain_text_edit->setPlainText(QString::fromStdString(config_.ai.system_prompt));

  {
    security::DpapiSecretStore secret_store;
    auto decrypted_token = secret_store.Decrypt(config_.security.encrypted_api_key);
    if (decrypted_token.has_value()) {
      ui_->api_token_line_edit->setText(QString::fromStdString(*decrypted_token));
    } else {
      ui_->api_token_line_edit->clear();
    }
  }

  ui_->debug_enabled_check_box->setChecked(config_.debug.enabled);
  ui_->show_performance_metrics_check_box->setChecked(config_.debug.show_performance_metrics);
  ui_->metrics_refresh_ms_spin_box->setValue(
      std::clamp(config_.debug.metrics_refresh_ms, kMinMetricsRefreshMs, kMaxMetricsRefreshMs));

  UpdateAiControlState();
  updating_ui_ = false;

  UpdatePerformanceTimerState();
  UpdatePerformanceSnapshotUi();
}

void SettingsWindow::ReadUiToConfig() {
  config_.skin.enable_live2d = ui_->live2d_enabled_check_box->isChecked();
  config_.skin.enable_eye_tracking = ui_->eye_tracking_enabled_check_box->isChecked();
  config_.skin.enable_idle_animation = ui_->idle_animation_enabled_check_box->isChecked();
  config_.skin.idle_interval_seconds = ui_->idle_interval_spin_box->value();
  config_.skin.idle_motion_group = ui_->idle_group_line_edit->text().trimmed().toStdString();
  if (config_.skin.idle_motion_group.empty()) {
    config_.skin.idle_motion_group = "Idle";
  }
  config_.skin.model_width_px = std::clamp(ui_->model_width_spin_box->value(), kMinModelDimensionPx,
                                           kMaxModelDimensionPx);
  config_.skin.model_height_px =
      std::clamp(ui_->model_height_spin_box->value(), kMinModelDimensionPx, kMaxModelDimensionPx);

  config_.window.always_on_top = ui_->always_on_top_check_box->isChecked();
  config_.window.auto_fit_model_rect = ui_->auto_fit_model_rect_check_box->isChecked();
  config_.window.min_model_window_padding_px =
      std::clamp(ui_->model_window_padding_spin_box->value(), kMinWindowPaddingPx, kMaxWindowPaddingPx);

  config_.skin.directory = ui_->skin_root_line_edit->text().trimmed().toStdString();
  const QVariant selected_skin = ui_->skin_selector_combo_box->currentData();
  if (selected_skin.isValid()) {
    config_.skin.current = selected_skin.toString().toStdString();
  } else if (ui_->skin_selector_combo_box->count() > 0) {
    config_.skin.current = ui_->skin_selector_combo_box->currentText().toStdString();
  }

  config_.ai.inference_mode = InferenceModeFromComboIndex(ui_->inference_mode_combo_box->currentIndex());
  config_.ai.provider = AiProviderFromComboIndex(ui_->provider_combo_box->currentIndex());
  config_.ai.stream = ui_->stream_enabled_check_box->isChecked();
  config_.ai.request_timeout_ms = ui_->request_timeout_ms_spin_box->value();
  config_.ai.max_tokens = ui_->max_tokens_spin_box->value();
  config_.ai.temperature = ui_->temperature_double_spin_box->value();
  config_.ai.top_p = ui_->top_p_double_spin_box->value();
  config_.ai.api_base_url = ui_->api_base_url_line_edit->text().trimmed().toStdString();
  config_.ai.api_model = ui_->api_model_line_edit->text().trimmed().toStdString();
  config_.ai.local_model_path = ui_->local_model_path_line_edit->text().trimmed().toStdString();
  config_.ai.local_gpu_layers = ui_->local_gpu_layers_spin_box->value();
  config_.ai.local_threads = ui_->local_threads_spin_box->value();
  config_.ai.local_context_length = ui_->local_context_length_spin_box->value();
  config_.ai.context_rounds = ui_->context_rounds_spin_box->value();
  config_.ai.system_prompt = ui_->system_prompt_plain_text_edit->toPlainText().trimmed().toStdString();

  const std::string plain_api_token = ui_->api_token_line_edit->text().trimmed().toStdString();
  if (plain_api_token.empty()) {
    config_.security.encrypted_api_key.clear();
  } else {
    security::DpapiSecretStore secret_store;
    auto encrypted_token = secret_store.Encrypt(plain_api_token);
    if (encrypted_token.has_value()) {
      config_.security.encrypted_api_key = *encrypted_token;
    } else {
      spdlog::warn("Failed to encrypt API token from settings. API token was cleared.");
      config_.security.encrypted_api_key.clear();
    }
  }

  config_.debug.enabled = ui_->debug_enabled_check_box->isChecked();
  config_.debug.show_performance_metrics = ui_->show_performance_metrics_check_box->isChecked();
  config_.debug.metrics_refresh_ms = std::clamp(ui_->metrics_refresh_ms_spin_box->value(),
                                                kMinMetricsRefreshMs, kMaxMetricsRefreshMs);
}

void SettingsWindow::RefreshSkinCandidates() {
  const QString skin_root_text = ui_->skin_root_line_edit->text().trimmed();
  if (!skin_root_text.isEmpty()) {
    config_.skin.directory = skin_root_text.toStdString();
  }

  available_skin_directories_ = DiscoverSkinDirectories();
  ui_->skin_selector_combo_box->clear();

  if (available_skin_directories_.empty()) {
    ui_->skin_selector_combo_box->addItem(QString::fromStdString(config_.skin.current),
                                          QString::fromStdString(config_.skin.current));
    ui_->status_label->setText(QStringLiteral("未发现可用皮套目录（需要包含 .model3.json）"));
    return;
  }

  const std::filesystem::path normalized_skin_root = NormalizePath(config_.skin.directory);
  int selected_index = -1;
  for (std::size_t index = 0; index < available_skin_directories_.size(); ++index) {
    std::error_code error_code;
    std::filesystem::path relative_path =
        std::filesystem::relative(available_skin_directories_[index], normalized_skin_root, error_code);
    if (error_code || relative_path.empty()) {
      relative_path = available_skin_directories_[index];
    }

    const std::string relative_string = relative_path.generic_string();
    ui_->skin_selector_combo_box->addItem(QString::fromStdString(relative_string),
                                          QString::fromStdString(relative_string));
    if (relative_string == config_.skin.current) {
      selected_index = static_cast<int>(index);
    }
  }

  if (selected_index < 0) {
    selected_index = 0;
  }
  ui_->skin_selector_combo_box->setCurrentIndex(selected_index);
  ui_->status_label->setText(
      QString::fromStdString("已发现皮套数量: " + std::to_string(available_skin_directories_.size())));
}

void SettingsWindow::ImportSkinDirectory() {
  const QString source_directory = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择要导入的皮套目录"),
      QString::fromStdString(config_.skin.directory.generic_string()));
  if (source_directory.isEmpty()) {
    return;
  }

  const std::filesystem::path source_path = source_directory.toStdString();
  if (!ContainsModel3JsonRecursively(source_path)) {
    QMessageBox::warning(this, QStringLiteral("导入失败"),
                         QStringLiteral("所选目录及子目录中未找到 .model3.json。"));
    return;
  }

  const std::filesystem::path skin_root = ui_->skin_root_line_edit->text().trimmed().toStdString();
  if (skin_root.empty()) {
    QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("请先设置皮套根目录。"));
    return;
  }

  std::error_code error_code;
  std::filesystem::create_directories(skin_root, error_code);
  if (error_code) {
    QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("无法创建皮套根目录。"));
    return;
  }

  std::string folder_name = source_path.filename().string();
  if (folder_name.empty()) {
    folder_name = "imported_skin";
  }

  std::filesystem::path target_directory = skin_root / folder_name;
  if (std::filesystem::exists(target_directory, error_code)) {
    const std::int64_t timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    target_directory = skin_root / (folder_name + "_" + std::to_string(timestamp));
  }

  std::filesystem::copy(source_path, target_directory,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        error_code);
  if (error_code) {
    QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("复制皮套目录失败。"));
    return;
  }

  spdlog::info("Skin imported from {} to {}", source_path.string(), target_directory.string());
  RefreshSkinCandidates();
  ui_->status_label->setText(QStringLiteral("皮套导入成功，可在下拉框中选择并保存。"));
}

void SettingsWindow::UpdateAiControlState() const {
  const bool use_local_model = ui_->inference_mode_combo_box->currentIndex() == 1;
  ui_->provider_combo_box->setEnabled(!use_local_model);
  ui_->api_base_url_line_edit->setEnabled(!use_local_model);
  ui_->api_model_line_edit->setEnabled(!use_local_model);
  ui_->api_token_line_edit->setEnabled(!use_local_model);

  ui_->local_model_path_line_edit->setEnabled(use_local_model);
  ui_->browse_local_model_button->setEnabled(use_local_model);
  ui_->local_gpu_layers_spin_box->setEnabled(use_local_model);
  ui_->local_threads_spin_box->setEnabled(use_local_model);
  ui_->local_context_length_spin_box->setEnabled(use_local_model);
}

void SettingsWindow::UpdatePerformanceSnapshotUi() {
  if (!ui_->show_performance_metrics_check_box->isChecked() || !get_performance_snapshot_) {
    ui_->perf_cpu_value_label->setText(QStringLiteral("--"));
    ui_->perf_gpu_value_label->setText(QStringLiteral("--"));
    ui_->perf_memory_value_label->setText(QStringLiteral("--"));
    ui_->perf_gpu_memory_value_label->setText(QStringLiteral("--"));
    return;
  }

  const diagnostics::PerformanceSnapshot snapshot = get_performance_snapshot_();
  ui_->perf_cpu_value_label->setText(QString::asprintf("%.1f %%", snapshot.cpu_process_percent));
  if (snapshot.gpu_available && snapshot.gpu_process_percent >= 0.0) {
    ui_->perf_gpu_value_label->setText(QString::asprintf("%.1f %%", snapshot.gpu_process_percent));
  } else {
    ui_->perf_gpu_value_label->setText(QStringLiteral("N/A"));
  }

  ui_->perf_memory_value_label->setText(
      QStringLiteral("%1 (WS) / %2 (Private)")
          .arg(FormatBytes(snapshot.process_working_set_bytes),
               FormatBytes(snapshot.process_private_bytes)));

  if (snapshot.gpu_available) {
    ui_->perf_gpu_memory_value_label->setText(FormatBytes(snapshot.process_gpu_dedicated_bytes));
  } else {
    ui_->perf_gpu_memory_value_label->setText(QStringLiteral("N/A"));
  }
}

void SettingsWindow::UpdatePerformanceTimerState() {
  if (!ui_->show_performance_metrics_check_box->isChecked() || !get_performance_snapshot_) {
    performance_timer_.stop();
    UpdatePerformanceSnapshotUi();
    return;
  }

  const int interval_ms =
      std::clamp(ui_->metrics_refresh_ms_spin_box->value(), kMinMetricsRefreshMs, kMaxMetricsRefreshMs);
  performance_timer_.setInterval(interval_ms);
  performance_timer_.start();
  UpdatePerformanceSnapshotUi();
}

QString SettingsWindow::FormatBytes(std::uint64_t bytes) {
  static constexpr double kUnit = 1024.0;

  const char* suffix = "B";
  double value = static_cast<double>(bytes);
  if (value >= kUnit) {
    value /= kUnit;
    suffix = "KB";
  }
  if (value >= kUnit) {
    value /= kUnit;
    suffix = "MB";
  }
  if (value >= kUnit) {
    value /= kUnit;
    suffix = "GB";
  }

  return QString::number(value, 'f', value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2)) + " " + suffix;
}

bool SettingsWindow::SaveConfigToDisk() {
  auto save_result = config_store_.Save(config_path_, config_);
  if (!save_result.has_value()) {
    QMessageBox::warning(this, QStringLiteral("保存失败"),
                         QStringLiteral("写入配置文件失败，请检查磁盘权限。"));
    return false;
  }

  spdlog::info("Config saved from settings panel: {}", config_path_.string());
  return true;
}

std::vector<std::filesystem::path> SettingsWindow::DiscoverSkinDirectories() const {
  std::vector<std::filesystem::path> discovered_directories;
  const std::filesystem::path skin_root = config_.skin.directory;

  std::error_code error_code;
  if (!std::filesystem::exists(skin_root, error_code) ||
      !std::filesystem::is_directory(skin_root, error_code)) {
    return discovered_directories;
  }

  std::filesystem::recursive_directory_iterator iterator(
      skin_root, std::filesystem::directory_options::skip_permission_denied, error_code);
  std::filesystem::recursive_directory_iterator end;
  for (; !error_code && iterator != end; iterator.increment(error_code)) {
    if (!iterator->is_directory(error_code)) {
      continue;
    }
    if (!ContainsModel3JsonInDirectory(iterator->path())) {
      continue;
    }
    discovered_directories.push_back(NormalizePath(iterator->path()));
  }

  std::sort(discovered_directories.begin(), discovered_directories.end());
  discovered_directories.erase(std::unique(discovered_directories.begin(), discovered_directories.end()),
                               discovered_directories.end());
  return discovered_directories;
}

}  // namespace mikudesk::ui

