#include "app/config.hpp"

#include <algorithm>
#include <cctype>

namespace mikudesk::app {

namespace {

std::string ToLower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

}  // namespace

std::string LogLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace:
      return "trace";
    case LogLevel::kDebug:
      return "debug";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kWarn:
      return "warn";
    case LogLevel::kError:
      return "error";
    case LogLevel::kCritical:
      return "critical";
  }
  return "info";
}

LogLevel LogLevelFromString(const std::string& value) {
  const std::string lowered = ToLower(value);
  if (lowered == "trace") {
    return LogLevel::kTrace;
  }
  if (lowered == "debug") {
    return LogLevel::kDebug;
  }
  if (lowered == "warn" || lowered == "warning") {
    return LogLevel::kWarn;
  }
  if (lowered == "error") {
    return LogLevel::kError;
  }
  if (lowered == "critical") {
    return LogLevel::kCritical;
  }
  return LogLevel::kInfo;
}

std::string DumpTypeToString(DumpType value) {
  switch (value) {
    case DumpType::kNormal:
      return "normal";
    case DumpType::kFull:
      return "full";
  }
  return "normal";
}

DumpType DumpTypeFromString(const std::string& value) {
  const std::string lowered = ToLower(value);
  if (lowered == "full") {
    return DumpType::kFull;
  }
  return DumpType::kNormal;
}

std::string InferenceModeToString(InferenceMode value) {
  switch (value) {
    case InferenceMode::kTokenApi:
      return "token_api";
    case InferenceMode::kLocalModel:
      return "local_model";
  }
  return "token_api";
}

InferenceMode InferenceModeFromString(const std::string& value) {
  const std::string lowered = ToLower(value);
  if (lowered == "local_model") {
    return InferenceMode::kLocalModel;
  }
  return InferenceMode::kTokenApi;
}

void to_json(nlohmann::json& json, const WindowConfig& config) {
  json = nlohmann::json{
      {"width", config.width},
      {"height", config.height},
      {"idle_fps", config.idle_fps},
      {"interactive_fps", config.interactive_fps},
      {"opacity", config.opacity},
      {"always_on_top", config.always_on_top},
      {"enable_window", config.enable_window},
  };
}

void from_json(const nlohmann::json& json, WindowConfig& config) {
  config.width = json.value("width", config.width);
  config.height = json.value("height", config.height);
  config.idle_fps = json.value("idle_fps", config.idle_fps);
  config.interactive_fps = json.value("interactive_fps", config.interactive_fps);
  config.opacity = json.value("opacity", config.opacity);
  config.always_on_top = json.value("always_on_top", config.always_on_top);
  config.enable_window = json.value("enable_window", config.enable_window);
}

void to_json(nlohmann::json& json, const LogConfig& config) {
  json = nlohmann::json{
      {"debug_mode", config.debug_mode},
      {"log_level", LogLevelToString(config.log_level)},
      {"log_directory", config.log_directory.string()},
      {"log_file_name", config.log_file_name},
      {"max_file_size_mb", config.max_file_size_mb},
      {"max_file_count", config.max_file_count},
  };
}

void from_json(const nlohmann::json& json, LogConfig& config) {
  config.debug_mode = json.value("debug_mode", config.debug_mode);
  config.log_level = LogLevelFromString(json.value("log_level", LogLevelToString(config.log_level)));
  config.log_directory = json.value("log_directory", config.log_directory.string());
  config.log_file_name = json.value("log_file_name", config.log_file_name);
  config.max_file_size_mb = json.value("max_file_size_mb", config.max_file_size_mb);
  config.max_file_count = json.value("max_file_count", config.max_file_count);
}

void to_json(nlohmann::json& json, const CrashConfig& config) {
  json = nlohmann::json{
      {"enable_handler", config.enable_handler},
      {"write_minidump", config.write_minidump},
      {"dump_type", DumpTypeToString(config.dump_type)},
      {"dump_directory", config.dump_directory.string()},
  };
}

void from_json(const nlohmann::json& json, CrashConfig& config) {
  config.enable_handler = json.value("enable_handler", config.enable_handler);
  config.write_minidump = json.value("write_minidump", config.write_minidump);
  config.dump_type = DumpTypeFromString(json.value("dump_type", DumpTypeToString(config.dump_type)));
  config.dump_directory = json.value("dump_directory", config.dump_directory.string());
}

void to_json(nlohmann::json& json, const SecurityConfig& config) {
  json = nlohmann::json{
      {"encrypted_api_key", config.encrypted_api_key},
  };
}

void from_json(const nlohmann::json& json, SecurityConfig& config) {
  config.encrypted_api_key = json.value("encrypted_api_key", config.encrypted_api_key);
}

void to_json(nlohmann::json& json, const SkinConfig& config) {
  json = nlohmann::json{
      {"directory", config.directory.string()},
      {"current", config.current},
      {"enable_live2d", config.enable_live2d},
      {"enable_eye_tracking", config.enable_eye_tracking},
      {"enable_idle_animation", config.enable_idle_animation},
      {"idle_motion_group", config.idle_motion_group},
      {"idle_interval_seconds", config.idle_interval_seconds},
      {"model_width_px", config.model_width_px},
      {"model_height_px", config.model_height_px},
  };
}

void from_json(const nlohmann::json& json, SkinConfig& config) {
  config.directory = json.value("directory", config.directory.string());
  config.current = json.value("current", config.current);
  config.enable_live2d = json.value("enable_live2d", config.enable_live2d);
  config.enable_eye_tracking = json.value("enable_eye_tracking", config.enable_eye_tracking);
  config.enable_idle_animation = json.value("enable_idle_animation", config.enable_idle_animation);
  config.idle_motion_group = json.value("idle_motion_group", config.idle_motion_group);
  config.idle_interval_seconds = json.value("idle_interval_seconds", config.idle_interval_seconds);
  config.model_width_px = std::clamp(json.value("model_width_px", config.model_width_px), 64, 4096);
  config.model_height_px =
      std::clamp(json.value("model_height_px", config.model_height_px), 64, 4096);
}

void to_json(nlohmann::json& json, const AiConfig& config) {
  json = nlohmann::json{
      {"inference_mode", InferenceModeToString(config.inference_mode)},
      {"api_base_url", config.api_base_url},
      {"api_model", config.api_model},
      {"local_model_path", config.local_model_path.string()},
  };
}

void from_json(const nlohmann::json& json, AiConfig& config) {
  config.inference_mode =
      InferenceModeFromString(json.value("inference_mode", InferenceModeToString(config.inference_mode)));
  config.api_base_url = json.value("api_base_url", config.api_base_url);
  config.api_model = json.value("api_model", config.api_model);
  config.local_model_path = json.value("local_model_path", config.local_model_path.string());
}

void to_json(nlohmann::json& json, const DebugConfig& config) {
  json = nlohmann::json{
      {"enabled", config.enabled},
      {"smoke_test_seconds", config.smoke_test_seconds},
      {"crash_test", config.crash_test},
      {"show_performance_metrics", config.show_performance_metrics},
      {"metrics_refresh_ms", config.metrics_refresh_ms},
  };
}

void from_json(const nlohmann::json& json, DebugConfig& config) {
  config.enabled = json.value("enabled", config.enabled);
  config.smoke_test_seconds = json.value("smoke_test_seconds", config.smoke_test_seconds);
  config.crash_test = json.value("crash_test", config.crash_test);
  config.show_performance_metrics =
      json.value("show_performance_metrics", config.show_performance_metrics);
  config.metrics_refresh_ms = std::clamp(json.value("metrics_refresh_ms", config.metrics_refresh_ms),
                                         250, 10000);
}

void to_json(nlohmann::json& json, const AppConfig& config) {
  json = nlohmann::json{
      {"window", config.window},
      {"log", config.log},
      {"crash", config.crash},
      {"security", config.security},
      {"skin", config.skin},
      {"ai", config.ai},
      {"debug", config.debug},
  };
}

void from_json(const nlohmann::json& json, AppConfig& config) {
  if (json.contains("window")) {
    config.window = json.at("window").get<WindowConfig>();
  }
  if (json.contains("log")) {
    config.log = json.at("log").get<LogConfig>();
  }
  if (json.contains("crash")) {
    config.crash = json.at("crash").get<CrashConfig>();
  }
  if (json.contains("security")) {
    config.security = json.at("security").get<SecurityConfig>();
  }
  if (json.contains("skin")) {
    config.skin = json.at("skin").get<SkinConfig>();
  }
  if (json.contains("ai")) {
    config.ai = json.at("ai").get<AiConfig>();
  }
  if (json.contains("debug")) {
    config.debug = json.at("debug").get<DebugConfig>();
  }
}

}  // namespace mikudesk::app
