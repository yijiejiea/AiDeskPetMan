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
    if (!Expect(result->window.auto_fit_model_rect, "default window auto-fit should be true")) {
      return 1;
    }
    if (!Expect(result->window.min_model_window_padding_px == 0,
                "default window model padding should be 0")) {
      return 1;
    }
    if (!Expect(result->ai.provider == mikudesk::app::AiProvider::kOpenAi,
                "default AI provider should be openai")) {
      return 1;
    }
    if (!Expect(result->ai.stream, "default AI stream should be true")) {
      return 1;
    }
    if (!Expect(result->ai.context_rounds == 10, "default AI context rounds should be 10")) {
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
    write_config.window.auto_fit_model_rect = true;
    write_config.window.min_model_window_padding_px = 12;
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
    write_config.ai.provider = mikudesk::app::AiProvider::kDeepSeek;
    write_config.ai.api_base_url = "https://api.openai.com/v1";
    write_config.ai.api_model = "gpt-4o-mini";
    write_config.ai.stream = true;
    write_config.ai.request_timeout_ms = 45000;
    write_config.ai.max_tokens = 2048;
    write_config.ai.temperature = 0.6;
    write_config.ai.top_p = 0.95;
    write_config.ai.system_prompt = "You are MikuDesk.";
    write_config.ai.context_rounds = 16;
    write_config.ai.local_model_path = "assets/models/qwen.gguf";
    write_config.ai.local_gpu_layers = 35;
    write_config.ai.local_threads = 12;
    write_config.ai.local_context_length = 8192;
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
    if (!Expect(read_result->window.auto_fit_model_rect == write_config.window.auto_fit_model_rect,
                "roundtrip window auto-fit mismatch")) {
      return 1;
    }
    if (!Expect(read_result->window.min_model_window_padding_px ==
                    write_config.window.min_model_window_padding_px,
                "roundtrip window model padding mismatch")) {
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
    if (!Expect(read_result->ai.provider == write_config.ai.provider,
                "roundtrip AI provider mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.stream == write_config.ai.stream, "roundtrip AI stream mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.request_timeout_ms == write_config.ai.request_timeout_ms,
                "roundtrip AI timeout mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.max_tokens == write_config.ai.max_tokens,
                "roundtrip AI max tokens mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.temperature == write_config.ai.temperature,
                "roundtrip AI temperature mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.top_p == write_config.ai.top_p,
                "roundtrip AI top_p mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.system_prompt == write_config.ai.system_prompt,
                "roundtrip AI system prompt mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.context_rounds == write_config.ai.context_rounds,
                "roundtrip AI context rounds mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.local_model_path == write_config.ai.local_model_path,
                "roundtrip local model path mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.local_gpu_layers == write_config.ai.local_gpu_layers,
                "roundtrip local gpu layers mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.local_threads == write_config.ai.local_threads,
                "roundtrip local threads mismatch")) {
      return 1;
    }
    if (!Expect(read_result->ai.local_context_length == write_config.ai.local_context_length,
                "roundtrip local context length mismatch")) {
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
