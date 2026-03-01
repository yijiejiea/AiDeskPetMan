#include "diagnostics/crash_handler.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "spdlog/spdlog.h"

#if defined(_WIN32)
#include <windows.h>

#include <dbghelp.h>
#endif

namespace mikudesk::diagnostics {

namespace {

std::mutex g_mutex;
bool g_installed = false;
app::CrashConfig g_config;

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;

std::string BuildTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_s(&local_time, &now_time);

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y%m%d_%H%M%S");
  return stream.str();
}

void CaptureStackTrace(CONTEXT* context) {
  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();

  STACKFRAME64 frame{};
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_X64)
  DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = context->Rip;
  frame.AddrStack.Offset = context->Rsp;
  frame.AddrFrame.Offset = context->Rbp;
#else
  DWORD machine_type = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = context->Eip;
  frame.AddrStack.Offset = context->Esp;
  frame.AddrFrame.Offset = context->Ebp;
#endif

  spdlog::critical("--- Stack Trace ---");

  for (int index = 0; index < 64; ++index) {
    const BOOL walked = StackWalk64(machine_type, process, thread, &frame, context, nullptr,
                                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
    if (!walked || frame.AddrPC.Offset == 0) {
      break;
    }

    char symbol_storage[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    DWORD64 displacement = 0;
    if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
      spdlog::critical("  #{:02d} {} + 0x{:X}", index, symbol->Name, displacement);
    } else {
      spdlog::critical("  #{:02d} 0x{:016X}", index, frame.AddrPC.Offset);
    }
  }
}

void WriteMiniDump(EXCEPTION_POINTERS* exception_pointers) {
  if (!g_config.write_minidump) {
    return;
  }

  std::error_code error_code;
  std::filesystem::create_directories(g_config.dump_directory, error_code);
  if (error_code) {
    spdlog::critical("Failed to create dump directory: {}", g_config.dump_directory.string());
    return;
  }

  const std::filesystem::path dump_path =
      g_config.dump_directory / ("crash_" + BuildTimestamp() + ".dmp");

  HANDLE file = CreateFileA(dump_path.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    spdlog::critical("Failed to create dump file: {}", dump_path.string());
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_info{};
  exception_info.ThreadId = GetCurrentThreadId();
  exception_info.ExceptionPointers = exception_pointers;
  exception_info.ClientPointers = FALSE;

  MINIDUMP_TYPE dump_type = MiniDumpNormal;
  if (g_config.dump_type == app::DumpType::kFull) {
    dump_type = MiniDumpWithFullMemory;
  }

  const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dump_type,
                                        &exception_info, nullptr, nullptr);
  CloseHandle(file);

  if (!dumped) {
    spdlog::critical("MiniDumpWriteDump failed with error {}", GetLastError());
  } else {
    spdlog::critical("Crash dump saved: {}", dump_path.string());
  }
}

LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS* exception_pointers) {
  if (exception_pointers == nullptr || exception_pointers->ExceptionRecord == nullptr ||
      exception_pointers->ContextRecord == nullptr) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  const auto* exception_record = exception_pointers->ExceptionRecord;
  spdlog::critical("=== UNHANDLED EXCEPTION ===");
  spdlog::critical("Exception code: 0x{:08X}", exception_record->ExceptionCode);
  spdlog::critical("Exception address: 0x{:016X}",
                   reinterpret_cast<std::uintptr_t>(exception_record->ExceptionAddress));

  CaptureStackTrace(exception_pointers->ContextRecord);
  WriteMiniDump(exception_pointers);
  if (auto logger = spdlog::default_logger(); logger != nullptr) {
    logger->flush();
  }
  spdlog::shutdown();
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}  // namespace

app::Result<void> CrashHandler::Install(const app::CrashConfig& config) {
  std::scoped_lock lock(g_mutex);
  if (g_installed) {
    return {};
  }

  g_config = config;
  if (!config.enable_handler) {
    g_installed = true;
    return {};
  }

#if defined(_WIN32)
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  if (!SymInitialize(process, nullptr, TRUE)) {
    return std::unexpected(app::AppError::kCrashHandlerInstallFailed);
  }

  g_previous_filter = SetUnhandledExceptionFilter(UnhandledExceptionFilterFn);
  g_installed = true;
  spdlog::info("CrashHandler installed");
  return {};
#else
  return std::unexpected(app::AppError::kCrashHandlerInstallFailed);
#endif
}

void CrashHandler::Uninstall() {
  std::scoped_lock lock(g_mutex);
  if (!g_installed) {
    return;
  }

#if defined(_WIN32)
  if (g_config.enable_handler) {
    SetUnhandledExceptionFilter(g_previous_filter);
    SymCleanup(GetCurrentProcess());
  }
#endif
  g_installed = false;
}

}  // namespace mikudesk::diagnostics
