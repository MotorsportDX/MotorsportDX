/**
 * @file        graphics/d3d12/shader_compile_queue.cpp
 * @brief       Shader override and mod compiles off the thread that issues draws
 */

#include <rex/graphics/d3d12/shader_compile_queue.h>

#include <algorithm>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/thread.h>
#include <rex/ui/d3d12/d3d12_api.h>

#include "../util/perf_trace.h"

REXCVAR_DEFINE_BOOL(shader_override_async, true, "GPU/D3D12",
                    "Compile a shader override or mod that a draw meets and "
                    "shader_override_cache_path does not hold on a worker: the draw keeps the DXBC "
                    "translation (the game's own look) until the compiled shader is swapped in "
                    "(docs/analysis/d3d-hle-n6.md 19.3)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::graphics::d3d12 {

void ShaderCompileQueue::Push(D3D12Shader::D3D12Translation* translation, Job job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    StartWorkers();
    // Counted before a worker can take it, so the count never goes below zero.
    perf_trace::mods_pending.fetch_add(1, std::memory_order_relaxed);
    jobs_.emplace_back(translation, std::move(job));
  }
  cond_.notify_one();
}

void ShaderCompileQueue::PushPipeline(void* pipeline, PipelineJob job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    StartWorkers();
    pipeline_jobs_.emplace_back(pipeline, std::move(job));
  }
  cond_.notify_one();
}

void ShaderCompileQueue::StartWorkers() {
  if (!threads_.empty()) {
    return;
  }
  // FXC takes 0.1 to 2.4 s a mod (d3d-hle-n6.md 19.2.1); the title's threads
  // keep the other processors.
  uint32_t count =
      std::clamp(rex::thread::logical_processor_count() / 4, uint32_t(1), uint32_t(4));
  for (uint32_t i = 0; i < count; ++i) {
    threads_.emplace_back([this]() { WorkerMain(); });
  }
  REXGPU_INFO("Shader overrides: {} compile workers", count);
}

void ShaderCompileQueue::TakeResults(std::vector<Result>& out) {
  std::lock_guard<std::mutex> lock(results_mutex_);
  for (Result& result : results_) {
    out.push_back(std::move(result));
  }
  results_.clear();
  result_count_.store(0, std::memory_order_release);
}

void ShaderCompileQueue::TakePipelineResults(std::vector<PipelineResult>& out) {
  std::lock_guard<std::mutex> lock(results_mutex_);
  out.insert(out.end(), pipeline_results_.begin(), pipeline_results_.end());
  pipeline_results_.clear();
  pipeline_result_count_.store(0, std::memory_order_release);
}

void ShaderCompileQueue::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    jobs_.clear();
    pipeline_jobs_.clear();
  }
  cond_.notify_all();
  for (std::thread& thread : threads_) {
    thread.join();
  }
  threads_.clear();
  std::lock_guard<std::mutex> lock(results_mutex_);
  results_.clear();
  result_count_.store(0, std::memory_order_release);
  for (const PipelineResult& result : pipeline_results_) {
    if (result.state) {
      result.state->Release();
    }
  }
  pipeline_results_.clear();
  pipeline_result_count_.store(0, std::memory_order_release);
}

void ShaderCompileQueue::WorkerMain() {
  rex::thread::set_current_thread_name("Shader Mod Compile");
  // Behind the title's threads: a mod that shows a little later costs less than
  // a longer frame.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    cond_.wait(lock,
               [this]() { return stop_ || !jobs_.empty() || !pipeline_jobs_.empty(); });
    if (stop_) {
      return;
    }
    if (!pipeline_jobs_.empty()) {
      auto [pipeline, pipeline_job] = std::move(pipeline_jobs_.front());
      pipeline_jobs_.pop_front();
      lock.unlock();
      PipelineResult result{pipeline, pipeline_job()};
      {
        std::lock_guard<std::mutex> results_lock(results_mutex_);
        pipeline_results_.push_back(result);
        pipeline_result_count_.store(pipeline_results_.size(), std::memory_order_release);
      }
      lock.lock();
      continue;
    }
    auto [translation, job] = std::move(jobs_.front());
    jobs_.pop_front();
    lock.unlock();
    Result result{translation, job()};
    {
      std::lock_guard<std::mutex> results_lock(results_mutex_);
      results_.push_back(std::move(result));
      result_count_.store(results_.size(), std::memory_order_release);
    }
    lock.lock();
  }
}

}  // namespace rex::graphics::d3d12
