#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "nlohmann/json.hpp"

namespace mikudesk::app {

enum class LogLevel : std::uint8_t {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kCritical,
};

enum class DumpType : std::uint8_t {
  kNormal,
  kFull,
};

enum class InferenceMode : std::uint8_t {
  kTokenApi,
  kLocalModel,
};

struct WindowConfig {
  int width = 400;
  int height = 600;
  int idle_fps = 30;
  int interactive_fps = 60;
  float opacity = 1.0F;
  bool always_on_top = true;
  bool enable_window = true;
};

struct LogConfig {
  bool debug_mode = false;
  LogLevel log_level = LogLevel::kInfo;
  std::filesystem::path log_directory = "logs";
  std::string log_file_name = "mikudesk.log";
  std::size_t max_file_size_mb = 10;
  std::size_t max_file_count = 5;
};

struct CrashConfig {
  bool enable_handler = true;
  bool write_minidump = true;
  DumpType dump_type = DumpType::kNormal;
  std::filesystem::path dump_directory = "logs";
};

struct SecurityConfig {
  std::string encrypted_api_key;
};

struct SkinConfig {
  std::filesystem::path directory = "assets/skins";
  std::string current = "miku_default";
  bool enable_live2d = true;
  bool enable_eye_tracking = true;
  bool enable_idle_animation = true;
  std::string idle_motion_group = "Idle";
  int idle_interval_seconds = 30;
  int model_width_px = 300;
  int model_height_px = 560;
};

struct AiConfig {
  InferenceMode inference_mode = InferenceMode::kTokenApi;
  std::string api_base_url = "https://api.openai.com/v1";
  std::string api_model = "gpt-4o-mini";
  std::filesystem::path local_model_path = "assets/models";
};

struct DebugConfig {
  bool enabled = false;
  int smoke_test_seconds = 0;
  bool crash_test = false;
  bool show_performance_metrics = true;
  int metrics_refresh_ms = 1000;
};

struct AppConfig {
  WindowConfig window;
  LogConfig log;
  CrashConfig crash;
  SecurityConfig security;
  SkinConfig skin;
  AiConfig ai;
  DebugConfig debug;
};

std::string LogLevelToString(LogLevel level);
LogLevel LogLevelFromString(const std::string& value);
std::string DumpTypeToString(DumpType value);
DumpType DumpTypeFromString(const std::string& value);
std::string InferenceModeToString(InferenceMode value);
InferenceMode InferenceModeFromString(const std::string& value);

void to_json(nlohmann::json& json, const WindowConfig& config);
void from_json(const nlohmann::json& json, WindowConfig& config);

void to_json(nlohmann::json& json, const LogConfig& config);
void from_json(const nlohmann::json& json, LogConfig& config);

void to_json(nlohmann::json& json, const CrashConfig& config);
void from_json(const nlohmann::json& json, CrashConfig& config);

void to_json(nlohmann::json& json, const SecurityConfig& config);
void from_json(const nlohmann::json& json, SecurityConfig& config);

void to_json(nlohmann::json& json, const SkinConfig& config);
void from_json(const nlohmann::json& json, SkinConfig& config);

void to_json(nlohmann::json& json, const AiConfig& config);
void from_json(const nlohmann::json& json, AiConfig& config);

void to_json(nlohmann::json& json, const DebugConfig& config);
void from_json(const nlohmann::json& json, DebugConfig& config);

void to_json(nlohmann::json& json, const AppConfig& config);
void from_json(const nlohmann::json& json, AppConfig& config);

}  // namespace mikudesk::app
