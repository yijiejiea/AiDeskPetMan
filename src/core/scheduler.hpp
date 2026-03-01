#pragma once

#include <deque>
#include <functional>
#include <mutex>

#include "core/task.hpp"

namespace mikudesk::core {

class Scheduler {
 public:
  using CallbackTask = std::function<void()>;
  using CoroutineTask = Task<void>;

  void PostTask(CallbackTask task);
  void PostCoroutine(CoroutineTask task);
  void Tick();
  void RequestStop();
  bool IsStopRequested() const;

 private:
  mutable std::mutex mutex_;
  std::deque<CallbackTask> pending_tasks_;
  std::deque<CoroutineTask> pending_coroutines_;
  bool stop_requested_ = false;
};

}  // namespace mikudesk::core
