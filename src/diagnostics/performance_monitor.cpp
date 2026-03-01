#include "diagnostics/performance_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

#if defined(_WIN32)
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <windows.h>
#endif

namespace mikudesk::diagnostics {

namespace {

constexpr int kMinMetricsRefreshMs = 250;
constexpr int kMaxMetricsRefreshMs = 10000;

#if defined(_WIN32)
std::uint64_t FileTimeToUint64(const FILETIME& file_time) {
  return (static_cast<std::uint64_t>(file_time.dwHighDateTime) << 32U) |
         static_cast<std::uint64_t>(file_time.dwLowDateTime);
}

std::wstring ToLower(std::wstring value) {
  std::ranges::transform(value, value.begin(), [](wchar_t character) {
    return static_cast<wchar_t>(towlower(character));
  });
  return value;
}

bool InstanceMatchesProcessId(const wchar_t* instance_name, std::uint32_t process_id) {
  if (instance_name == nullptr) {
    return false;
  }

  const std::wstring lowered_instance = ToLower(instance_name);
  const std::wstring needle = L"pid_" + std::to_wstring(process_id);
  return lowered_instance.find(needle) != std::wstring::npos;
}

bool QueryGpuEnginePercent(PDH_HCOUNTER counter, std::uint32_t process_id, double* sum_percent) {
  DWORD buffer_size = 0;
  DWORD item_count = 0;
  PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count,
                                                   nullptr);
  if (status != PDH_MORE_DATA) {
    return false;
  }

  std::vector<std::byte> buffer(buffer_size);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
  status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items);
  if (status != ERROR_SUCCESS) {
    return false;
  }

  double total_percent = 0.0;
  for (DWORD index = 0; index < item_count; ++index) {
    const auto& item = items[index];
    if (!InstanceMatchesProcessId(item.szName, process_id)) {
      continue;
    }
    if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
        item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
      continue;
    }
    total_percent += item.FmtValue.doubleValue;
  }

  *sum_percent = std::clamp(total_percent, 0.0, 100.0);
  return true;
}

bool QueryGpuMemoryBytes(PDH_HCOUNTER counter, std::uint32_t process_id, std::uint64_t* memory_bytes) {
  DWORD buffer_size = 0;
  DWORD item_count = 0;
  PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &buffer_size, &item_count,
                                                   nullptr);
  if (status != PDH_MORE_DATA) {
    return false;
  }

  std::vector<std::byte> buffer(buffer_size);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
  status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &buffer_size, &item_count, items);
  if (status != ERROR_SUCCESS) {
    return false;
  }

  std::uint64_t total_bytes = 0;
  for (DWORD index = 0; index < item_count; ++index) {
    const auto& item = items[index];
    if (!InstanceMatchesProcessId(item.szName, process_id)) {
      continue;
    }
    if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
        item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
      continue;
    }
    const auto raw_value = item.FmtValue.largeValue;
    if (raw_value > 0) {
      total_bytes += static_cast<std::uint64_t>(raw_value);
    }
  }

  *memory_bytes = total_bytes;
  return true;
}
#endif

}  // namespace

PerformanceMonitor::~PerformanceMonitor() {
#if defined(_WIN32)
  ReleaseGpuCounters();
#endif
}

app::Result<void> PerformanceMonitor::Initialize() {
  if (initialized_) {
    return {};
  }

  refresh_interval_ms_ = std::clamp(refresh_interval_ms_, kMinMetricsRefreshMs, kMaxMetricsRefreshMs);

#if defined(_WIN32)
  process_id_ = GetCurrentProcessId();

  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  logical_processor_count_ = std::max<DWORD>(system_info.dwNumberOfProcessors, 1U);

  FILETIME create_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  FILETIME system_time{};
  if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time) != 0) {
    GetSystemTimeAsFileTime(&system_time);
    last_process_kernel_time_ = FileTimeToUint64(kernel_time);
    last_process_user_time_ = FileTimeToUint64(user_time);
    last_system_time_ = FileTimeToUint64(system_time);
    has_cpu_baseline_ = true;
  }

  const bool gpu_initialized = InitializeGpuCounters();
  if (!gpu_initialized) {
    spdlog::warn("GPU performance counters are unavailable. GPU metrics will show N/A.");
  }
#endif

  initialized_ = true;
  return {};
}

void PerformanceMonitor::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

void PerformanceMonitor::SetRefreshIntervalMs(int refresh_interval_ms) {
  refresh_interval_ms_ =
      std::clamp(refresh_interval_ms, kMinMetricsRefreshMs, kMaxMetricsRefreshMs);
}

void PerformanceMonitor::Tick() {
  if (!initialized_ || !enabled_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (has_last_sample_time_) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_sample_time_);
    if (elapsed.count() < refresh_interval_ms_) {
      return;
    }
  }

  last_sample_time_ = now;
  has_last_sample_time_ = true;
  Sample();
}

PerformanceSnapshot PerformanceMonitor::GetSnapshot() const {
  std::lock_guard lock(snapshot_mutex_);
  return snapshot_;
}

void PerformanceMonitor::Sample() {
  std::lock_guard lock(snapshot_mutex_);

#if defined(_WIN32)
  SampleCpuAndMemory();
  SampleGpu();
#endif
}

#if defined(_WIN32)
bool PerformanceMonitor::InitializeGpuCounters() {
  PDH_HQUERY query = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
    return false;
  }

  PDH_HCOUNTER engine_counter = nullptr;
  PDH_HCOUNTER memory_counter = nullptr;
  const PDH_STATUS engine_status = PdhAddEnglishCounterW(
      query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engine_counter);
  const PDH_STATUS memory_status = PdhAddEnglishCounterW(
      query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &memory_counter);

  gpu_engine_available_ = engine_status == ERROR_SUCCESS;
  gpu_memory_available_ = memory_status == ERROR_SUCCESS;
  if (!gpu_engine_available_ && !gpu_memory_available_) {
    PdhCloseQuery(query);
    return false;
  }

  if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
    PdhCloseQuery(query);
    gpu_engine_available_ = false;
    gpu_memory_available_ = false;
    return false;
  }

  gpu_query_ = query;
  gpu_engine_counter_ = engine_counter;
  gpu_memory_counter_ = memory_counter;
  return true;
}

void PerformanceMonitor::SampleCpuAndMemory() {
  FILETIME create_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  FILETIME system_time{};
  if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time) != 0) {
    GetSystemTimeAsFileTime(&system_time);
    const std::uint64_t current_kernel_time = FileTimeToUint64(kernel_time);
    const std::uint64_t current_user_time = FileTimeToUint64(user_time);
    const std::uint64_t current_system_time = FileTimeToUint64(system_time);

    if (has_cpu_baseline_ && current_system_time > last_system_time_) {
      const std::uint64_t process_delta =
          (current_kernel_time - last_process_kernel_time_) +
          (current_user_time - last_process_user_time_);
      const std::uint64_t system_delta = current_system_time - last_system_time_;
      if (system_delta > 0) {
        const double usage =
            (static_cast<double>(process_delta) / static_cast<double>(system_delta)) * 100.0 /
            static_cast<double>(logical_processor_count_);
        snapshot_.cpu_process_percent = std::clamp(usage, 0.0, 100.0);
      }
    }

    has_cpu_baseline_ = true;
    last_process_kernel_time_ = current_kernel_time;
    last_process_user_time_ = current_user_time;
    last_system_time_ = current_system_time;
  }

  PROCESS_MEMORY_COUNTERS_EX memory_counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_counters),
                           sizeof(memory_counters)) != 0) {
    snapshot_.process_working_set_bytes = memory_counters.WorkingSetSize;
    snapshot_.process_private_bytes = memory_counters.PrivateUsage;
  }
}

void PerformanceMonitor::SampleGpu() {
  const auto query = reinterpret_cast<PDH_HQUERY>(gpu_query_);
  if (query == nullptr || PdhCollectQueryData(query) != ERROR_SUCCESS) {
    snapshot_.gpu_available = false;
    snapshot_.gpu_process_percent = -1.0;
    snapshot_.process_gpu_dedicated_bytes = 0;
    return;
  }

  bool has_any_gpu_metric = false;
  if (gpu_engine_available_) {
    double gpu_percent = 0.0;
    if (QueryGpuEnginePercent(reinterpret_cast<PDH_HCOUNTER>(gpu_engine_counter_), process_id_,
                              &gpu_percent)) {
      snapshot_.gpu_process_percent = gpu_percent;
      has_any_gpu_metric = true;
    } else {
      snapshot_.gpu_process_percent = -1.0;
    }
  } else {
    snapshot_.gpu_process_percent = -1.0;
  }

  if (gpu_memory_available_) {
    std::uint64_t gpu_memory_bytes = 0;
    if (QueryGpuMemoryBytes(reinterpret_cast<PDH_HCOUNTER>(gpu_memory_counter_), process_id_,
                            &gpu_memory_bytes)) {
      snapshot_.process_gpu_dedicated_bytes = gpu_memory_bytes;
      has_any_gpu_metric = true;
    } else {
      snapshot_.process_gpu_dedicated_bytes = 0;
    }
  } else {
    snapshot_.process_gpu_dedicated_bytes = 0;
  }

  snapshot_.gpu_available = has_any_gpu_metric;
}

void PerformanceMonitor::ReleaseGpuCounters() {
  if (gpu_query_ != nullptr) {
    PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(gpu_query_));
    gpu_query_ = nullptr;
  }
  gpu_engine_counter_ = nullptr;
  gpu_memory_counter_ = nullptr;
  gpu_engine_available_ = false;
  gpu_memory_available_ = false;
}
#endif

}  // namespace mikudesk::diagnostics
