/**
 * @file        graphics/d3d12/shader_compile_queue.h
 * @brief       Shader override and mod compiles off the thread that issues draws
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <rex/graphics/d3d12/shader.h>

struct ID3D12PipelineState;

namespace rex::graphics::d3d12 {

// shader_override_async (docs/analysis/d3d-hle-n6.md 19.3.2, part 1): the
// workers that compile a shader override or mod a draw met when the override
// cache did not hold it, and the binaries they compiled, until the thread that
// creates pipelines takes them (PipelineCache::SwapCompiledMods). Meanwhile the
// translation keeps its DXBC, the game's own look. The same workers then
// recreate the pipelines that existed with the compiled binary, ahead of other
// compiles, and keep the new states until the pipeline cache swaps them in. The
// workers start with the first job: a quarter of the logical processors, 1 to
// 4, below normal priority.
class ShaderCompileQueue {
 public:
  // The binary compiled for a translation, empty if the compile failed or did
  // not match the translation (its DXBC stays).
  struct Result {
    D3D12Shader::D3D12Translation* translation;
    std::vector<uint8_t> binary;
  };
  // Runs on a worker and returns the binary, or an empty one on a failure.
  using Job = std::function<std::vector<uint8_t>()>;
  // A pipeline state created for the pipeline cache's pipeline (an opaque
  // handle), nullptr if the creation failed.
  struct PipelineResult {
    void* pipeline;
    ID3D12PipelineState* state;
  };
  using PipelineJob = std::function<ID3D12PipelineState*()>;

  ShaderCompileQueue() = default;
  ShaderCompileQueue(const ShaderCompileQueue&) = delete;
  ShaderCompileQueue& operator=(const ShaderCompileQueue&) = delete;
  ~ShaderCompileQueue() { Stop(); }

  void Push(D3D12Shader::D3D12Translation* translation, Job job);
  // While one exists on a thread, the jobs that thread pushes wait behind every
  // other job: the start's predicted translations of pixel shaders that no
  // stored pipeline names (docs/analysis/d3d-hle-n6.md 19.3.10), so that what
  // the title draws compiles first.
  class BackgroundScope {
   public:
    BackgroundScope();
    ~BackgroundScope();
    BackgroundScope(const BackgroundScope&) = delete;
    BackgroundScope& operator=(const BackgroundScope&) = delete;
  };
  // Moves a translation's background job, if one waits, ahead of the other
  // compiles: a pipeline is created with it.
  void Promote(const D3D12Shader::D3D12Translation* translation);
  // Run before the compiles: a mod shows once its pipelines exist.
  void PushPipeline(void* pipeline, PipelineJob job);
  // Whether results wait for TakeResults or TakePipelineResults (any thread).
  bool HasResults() const { return result_count_.load(std::memory_order_acquire) != 0; }
  bool HasPipelineResults() const {
    return pipeline_result_count_.load(std::memory_order_acquire) != 0;
  }
  // Append the results since the last call to out, in the order they finished.
  void TakeResults(std::vector<Result>& out);
  void TakePipelineResults(std::vector<PipelineResult>& out);
  // Stops the workers, drops the jobs queued and the results not taken (the
  // pipeline states released), so that no translation or pipeline is
  // referenced any more.
  void Stop();

 private:
  // Needs mutex_.
  void StartWorkers();
  void WorkerMain();

  std::mutex mutex_;
  std::condition_variable cond_;
  std::deque<std::pair<D3D12Shader::D3D12Translation*, Job>> jobs_;
  std::deque<std::pair<D3D12Shader::D3D12Translation*, Job>> background_jobs_;
  std::atomic<size_t> background_job_count_{0};
  std::deque<std::pair<void*, PipelineJob>> pipeline_jobs_;
  bool stop_ = false;
  std::vector<std::thread> threads_;
  std::mutex results_mutex_;
  std::vector<Result> results_;
  std::atomic<size_t> result_count_{0};
  std::vector<PipelineResult> pipeline_results_;
  std::atomic<size_t> pipeline_result_count_{0};
};

}  // namespace rex::graphics::d3d12
