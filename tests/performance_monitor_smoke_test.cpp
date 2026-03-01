#include <chrono>
#include <iostream>
#include <thread>

#include "diagnostics/performance_monitor.hpp"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

}  // namespace

int main() {
  mikudesk::diagnostics::PerformanceMonitor performance_monitor;

  auto initialize_result = performance_monitor.Initialize();
  if (!Expect(initialize_result.has_value(), "Initialize should succeed")) {
    return 1;
  }

  performance_monitor.SetEnabled(true);
  performance_monitor.SetRefreshIntervalMs(250);

  for (int index = 0; index < 5; ++index) {
    performance_monitor.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(270));
  }

  const auto snapshot = performance_monitor.GetSnapshot();
  if (!Expect(snapshot.process_working_set_bytes > 0, "Working set should be greater than 0")) {
    return 1;
  }
  if (!Expect(snapshot.process_private_bytes > 0, "Private bytes should be greater than 0")) {
    return 1;
  }

  if (snapshot.gpu_available) {
    if (!Expect(snapshot.gpu_process_percent >= 0.0 && snapshot.gpu_process_percent <= 100.0,
                "GPU percent should be in [0, 100]")) {
      return 1;
    }
  } else {
    if (!Expect(snapshot.gpu_process_percent < 0.0, "GPU percent should be negative when unavailable")) {
      return 1;
    }
  }

  return 0;
}
