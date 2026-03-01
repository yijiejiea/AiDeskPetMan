#pragma once

#include <expected>
#include <string_view>

namespace mikudesk::app {

enum class AppError {
  kInvalidArgument,
  kWindowCreateFailed,
  kIoReadFailed,
  kIoWriteFailed,
  kJsonParseFailed,
  kLoggerInitFailed,
  kCrashHandlerInstallFailed,
  kSecretEncryptFailed,
  kSecretDecryptFailed,
  kLive2dNotEnabled,
  kLive2dSdkInvalid,
  kLive2dModelNotFound,
  kLive2dLoadFailed,
  kLive2dRenderFailed,
};

template <typename T>
using Result = std::expected<T, AppError>;

inline std::string_view AppErrorToString(AppError error) {
  switch (error) {
    case AppError::kInvalidArgument:
      return "invalid argument";
    case AppError::kWindowCreateFailed:
      return "window creation failed";
    case AppError::kIoReadFailed:
      return "file read failed";
    case AppError::kIoWriteFailed:
      return "file write failed";
    case AppError::kJsonParseFailed:
      return "JSON parse failed";
    case AppError::kLoggerInitFailed:
      return "logger initialization failed";
    case AppError::kCrashHandlerInstallFailed:
      return "crash handler install failed";
    case AppError::kSecretEncryptFailed:
      return "secret encryption failed";
    case AppError::kSecretDecryptFailed:
      return "secret decryption failed";
    case AppError::kLive2dNotEnabled:
      return "Live2D is not enabled";
    case AppError::kLive2dSdkInvalid:
      return "Live2D SDK path is invalid";
    case AppError::kLive2dModelNotFound:
      return "Live2D model not found";
    case AppError::kLive2dLoadFailed:
      return "Live2D load failed";
    case AppError::kLive2dRenderFailed:
      return "Live2D render failed";
  }
  return "unknown error";
}

}  // namespace mikudesk::app
