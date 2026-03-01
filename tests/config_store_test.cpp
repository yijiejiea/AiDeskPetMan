#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "resource/config_store.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

std::filesystem::path BuildTestRoot() {
  const auto id = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("mikudesk_config_store_test_" + id);
}

}  // namespace

int main() {
  using mikudesk::resource::ConfigStore;
  namespace fs = std::filesystem;

  const fs::path root = BuildTestRoot();
  const fs::path config_path = root / "config.json";
  fs::create_directories(root);

  ConfigStore config_store;

  {
    auto result = config_store.LoadOrDefault(config_path);
    if (!Expect(result.has_value(), "missing config should return defaults")) {
      return 1;
    }
    if (!Expect(result->window.width == 400, "default width should be 400")) {
      return 1;
    }
  }

  {
    std::ofstream bad_file(config_path, std::ios::trunc);
    bad_file << "{ bad json";
    auto result = config_store.LoadOrDefault(config_path);
    if (!Expect(result.has_value(), "invalid json should return defaults")) {
      return 1;
    }
    if (!Expect(result->window.height == 600, "default height should be 600")) {
      return 1;
    }
  }

  {
    mikudesk::app::AppConfig write_config;
    write_config.window.width = 1024;
    write_config.window.height = 768;
    write_config.log.log_file_name = "roundtrip.log";
    write_config.security.encrypted_api_key = "abc";
    write_config.skin.directory = "assets/skins";
    write_config.skin.current = "miku_default";
    write_config.skin.enable_live2d = true;
    write_config.skin.enable_eye_tracking = false;
    write_config.skin.enable_idle_animation = false;
    write_config.skin.idle_motion_group = "Idle";
    write_config.skin.idle_interval_seconds = 15;
    write_config.skin.model_width_px = 280;
    write_config.skin.model_height_px = 520;
    write_config.ai.inference_mode = mikudesk::app::InferenceMode::kLocalModel;
    write_config.ai.api_base_url = "https://api.openai.com/v1";
    write_config.ai.api_model = "gpt-4o-mini";
    write_config.ai.local_model_path = "assets/models/qwen.gguf";
    write_config.debug.enabled = true;
    write_config.debug.show_performance_metrics = false;
    write_config.debug.metrics_refresh_ms = 1500;

    auto save_result = config_store.Save(config_path, write_config);
    if (!Expect(save_result.has_value(), "save should succeed")) {
      return 1;
    }

    auto read_result = config_store.LoadOrDefault(config_path);
    if (!Expect(read_result.has_value(), "load after save should succeed")) {
      return 1;
    }
    if (!Expect(read_result->window.width == write_config.window.width, "roundtrip width mismatch")) {
      return 1;
    }
    if (!Expect(read_result->window.height == write_config.window.height,
                "roundtrip height mismatch")) {
      return 1;
    }
    if (!Expect(read_result->log.log_file_name == write_config.log.log_file_name,
                "roundtrip log file mismatch")) {
      return 1;
    }
    if (!Expect(read_result->security.encrypted_api_key == write_config.security.encrypted_api_key,
                "roundtrip key mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.current == write_config.skin.current, "roundtrip skin mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.enable_eye_tracking == write_config.skin.enable_eye_tracking,
                "roundtrip eye tracking mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.enable_idle_animation == write_config.skin.enable_idle_animation,
                "roundtrip idle animation mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.idle_interval_seconds == write_config.skin.idle_interval_seconds,
                "roundtrip idle interval mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.model_width_px == write_config.skin.model_width_px,
                "roundtrip model width mismatch")) {
      return 1;
    }
    if (!Expect(read_result->skin.model_height_px == write_config.skin.model_height_px,
                "roundtrip model height mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.inference_mode == write_config.ai.inference_mode,
                "roundtrip inference mode mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.local_model_path == write_config.ai.local_model_path,
                "roundtrip local model path mismatch")) {
      return 1;
    }
    if (!Expect(read_result->debug.enabled == write_config.debug.enabled,
                "roundtrip debug enabled mismatch")) {
      return 1;
    }
    if (!Expect(read_result->debug.show_performance_metrics ==
                    write_config.debug.show_performance_metrics,
                "roundtrip performance toggle mismatch")) {
      return 1;
    }
    if (!Expect(read_result->debug.metrics_refresh_ms == write_config.debug.metrics_refresh_ms,
                "roundtrip metrics refresh mismatch")) {
      return 1;
    }
  }

  std::error_code error_code;
  fs::remove_all(root, error_code);
  return 0;
}
