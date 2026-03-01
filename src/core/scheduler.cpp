#include "core/scheduler.hpp"

#include <utility>
#include <vector>

#include "spdlog/spdlog.h"

namespace mikudesk::core {

void Scheduler::PostTask(CallbackTask task) {
  std::scoped_lock lock(mutex_);
  pending_tasks_.push_back(std::move(task));
}

void Scheduler::PostCoroutine(CoroutineTask task) {
  std::scoped_lock lock(mutex_);
  pending_coroutines_.push_back(std::move(task));
}

void Scheduler::Tick() {
  std::vector<CallbackTask> tasks_to_run;
  std::vector<CoroutineTask> coroutines_to_resume;
  {
    std::scoped_lock lock(mutex_);
    if (stop_requested_) {
      return;
    }
    while (!pending_tasks_.empty()) {
      tasks_to_run.push_back(std::move(pending_tasks_.front()));
      pending_tasks_.pop_front();
    }
    while (!pending_coroutines_.empty()) {
      coroutines_to_resume.push_back(std::move(pending_coroutines_.front()));
      pending_coroutines_.pop_front();
    }
  }

  for (auto& task : tasks_to_run) {
    if (task) {
      task();
    }
  }

  for (auto& coroutine : coroutines_to_resume) {
    try {
      coroutine.Resume();
      if (!coroutine.IsDone()) {
        std::scoped_lock lock(mutex_);
        pending_coroutines_.push_back(std::move(coroutine));
      }
    } catch (const std::exception& exception) {
      spdlog::error("Coroutine task failed: {}", exception.what());
    } catch (...) {
      spdlog::error("Coroutine task failed with unknown exception.");
    }
  }
}

void Scheduler::RequestStop() {
  std::scoped_lock lock(mutex_);
  stop_requested_ = true;
  pending_tasks_.clear();
  pending_coroutines_.clear();
}

bool Scheduler::IsStopRequested() const {
  std::scoped_lock lock(mutex_);
  return stop_requested_;
}

}  // namespace mikudesk::core
