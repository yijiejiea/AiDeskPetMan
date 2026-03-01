#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

#include "app/app_error.hpp"

namespace mikudesk::diagnostics {

struct PerformanceSnapshot {
  double cpu_process_percent = 0.0;
  double gpu_process_percent = -1.0;
  std::uint64_t process_working_set_bytes = 0;
  std::uint64_t process_private_bytes = 0;
  std::uint64_t process_gpu_dedicated_bytes = 0;
  bool gpu_available = false;
};

class PerformanceMonitor {
 public:
  PerformanceMonitor() = default;
  ~PerformanceMonitor();

  PerformanceMonitor(const PerformanceMonitor&) = delete;
  PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

  app::Result<void> Initialize();
  void SetEnabled(bool enabled);
  void SetRefreshIntervalMs(int refresh_interval_ms);
  void Tick();
  PerformanceSnapshot GetSnapshot() const;

 private:
  void Sample();

#if defined(_WIN32)
  bool InitializeGpuCounters();
  void SampleCpuAndMemory();
  void SampleGpu();
  void ReleaseGpuCounters();
#endif

  mutable std::mutex snapshot_mutex_;
  PerformanceSnapshot snapshot_;
  bool initialized_ = false;
  bool enabled_ = true;
  int refresh_interval_ms_ = 1000;
  std::chrono::steady_clock::time_point last_sample_time_;
  bool has_last_sample_time_ = false;

#if defined(_WIN32)
  std::uint64_t last_process_kernel_time_ = 0;
  std::uint64_t last_process_user_time_ = 0;
  std::uint64_t last_system_time_ = 0;
  std::uint32_t logical_processor_count_ = 1;
  bool has_cpu_baseline_ = false;

  void* gpu_query_ = nullptr;
  void* gpu_engine_counter_ = nullptr;
  void* gpu_memory_counter_ = nullptr;
  bool gpu_engine_available_ = false;
  bool gpu_memory_available_ = false;
  std::uint32_t process_id_ = 0;
#endif
};

}  // namespace mikudesk::diagnostics
