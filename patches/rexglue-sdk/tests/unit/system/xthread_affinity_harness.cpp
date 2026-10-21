/**
 * @file        tests/unit/system/xthread_affinity_harness.cpp
 * @brief       G13: one warning per process and preserved guest CPU state
 *
 * This harness intentionally runs as its own process (not inside unit_tests)
 * because Runtime::Setup reserves the guest address space, which would clash
 * with the shared Memory instance used by the Catch2 suite.
 *
 * It restricts the process affinity mask to two logical processors before
 * Runtime::Setup, then:
 *   1. verifies EnableAffinityConfiguration() did not widen that mask;
 *   2. creates several suspended XThreads and calls SetActiveCpu from several
 *      host threads, checking every guest thread's final CPU;
 *   3. verifies that exactly one "Guest CPU affinity:" warning was emitted;
 *   4. exercises the explicit-affinity path with a guest CPU outside the
 *      process mask and verifies no disallowed bit was pinned.
 */

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/sinks/base_sink.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>

namespace {

class CountingSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  std::atomic<int> warning_count{0};
  std::string last_warning;

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    if (msg.level != spdlog::level::warn) {
      return;
    }
    std::string text(msg.payload.data(), msg.payload.size());
    if (text.find("Guest CPU affinity:") != std::string::npos) {
      warning_count.fetch_add(1, std::memory_order_relaxed);
      last_warning = std::move(text);
    }
  }

  void flush_() override {}
};

[[noreturn]] void Fail(const char* message) {
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::fflush(stderr);
  std::exit(1);
}

void Check(bool condition, const char* message) {
  if (!condition) {
    Fail(message);
  }
}

struct ProcessAffinityGuard {
  DWORD_PTR original_mask = 0;
  DWORD_PTR restricted_mask = 0;

  bool RestrictToTwoProcessors() {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
      return false;
    }
    original_mask = process_mask;

    DWORD_PTR chosen = 0;
    int selected = 0;
    for (int bit = 0; bit < static_cast<int>(sizeof(DWORD_PTR) * 8) && selected < 2; ++bit) {
      const DWORD_PTR candidate = static_cast<DWORD_PTR>(1) << bit;
      if ((process_mask & candidate) != 0) {
        chosen |= candidate;
        ++selected;
      }
    }
    if (selected < 2) {
      return false;
    }
    if (!SetProcessAffinityMask(GetCurrentProcess(), chosen)) {
      return false;
    }
    restricted_mask = chosen;
    return true;
  }

  void Restore() {
    if (original_mask != 0) {
      SetProcessAffinityMask(GetCurrentProcess(), original_mask);
      original_mask = 0;
    }
  }

  ~ProcessAffinityGuard() { Restore(); }
};

bool ThreadAffinityWithin(uint32_t thread_id, DWORD_PTR allowed_mask) {
  HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
  if (!handle) {
    return false;
  }
  GROUP_AFFINITY affinity = {};
  const bool ok = GetThreadGroupAffinity(handle, &affinity) != FALSE;
  CloseHandle(handle);
  if (!ok) {
    return false;
  }
  const DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.Mask);
  return mask != 0 && (mask & ~allowed_mask) == 0;
}

}  // namespace

using rex::X_STATUS;

int main() {
  rex::InitLogging();
  auto sink = std::make_shared<CountingSink>();
  rex::AddSink(sink);

  ProcessAffinityGuard affinity_guard;
  Check(affinity_guard.RestrictToTwoProcessors(),
        "could not restrict the process to two logical processors");

  rex::Runtime runtime("");
  rex::RuntimeConfig config;
  config.tool_mode = true;
  Check(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS, "Runtime::Setup failed in tool mode");

  // P2: EnableAffinityConfiguration() must preserve the startup mask.
  DWORD_PTR process_mask = 0;
  DWORD_PTR system_mask = 0;
  Check(GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) != FALSE,
        "GetProcessAffinityMask failed after Runtime::Setup");
  Check(process_mask == affinity_guard.restricted_mask,
        "EnableAffinityConfiguration widened or changed the process affinity mask");
  Check(rex::thread::process_affinity_mask() == affinity_guard.restricted_mask,
        "cached process affinity mask does not match the real process mask");
  Check(rex::thread::available_processor_count() == 2,
        "available_processor_count did not report the two permitted processors");

  auto* kernel_state = runtime.kernel_state();
  Check(kernel_state != nullptr, "Runtime has no kernel state");

  constexpr int kThreadCount = 4;
  constexpr int kIterations = 2000;
  const uint8_t assigned_cpu[kThreadCount] = {0, 1, 2, 3};
  std::vector<std::unique_ptr<rex::system::XThread>> guest_threads;
  guest_threads.reserve(kThreadCount);

  for (int i = 0; i < kThreadCount; ++i) {
    auto guest_thread =
        std::make_unique<rex::system::XThread>(kernel_state, 64 * 1024, 0, 0, 0,
                                               rex::system::X_CREATE_SUSPENDED,
                                               /*guest_thread=*/true);
    Check(guest_thread->Create() == X_STATUS_SUCCESS, "XThread::Create failed");
    guest_threads.push_back(std::move(guest_thread));
  }

  std::vector<std::thread> host_threads;
  host_threads.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    host_threads.emplace_back([&, i] {
      for (int iteration = 0; iteration < kIterations; ++iteration) {
        guest_threads[i]->SetActiveCpu(static_cast<uint8_t>((assigned_cpu[i] + 1) % 6));
        guest_threads[i]->SetActiveCpu(assigned_cpu[i]);
      }
    });
  }
  for (auto& thread : host_threads) {
    thread.join();
  }

  for (int i = 0; i < kThreadCount; ++i) {
    Check(guest_threads[i]->active_cpu() == assigned_cpu[i],
          "SetActiveCpu did not preserve the guest CPU state");
  }

  const int warnings_after_concurrency = sink->warning_count.load();
  Check(warnings_after_concurrency == 1,
        "expected exactly one guest CPU affinity warning for the whole process");

  // P2 explicit-affinity path: a guest CPU outside the process mask must not
  // cause SetThreadAffinityMask with a disallowed bit.
  Check(rex::cvar::SetFlagByName("ignore_thread_affinities", "false"),
        "could not enable explicit thread affinities");
  uint8_t disallowed_cpu = 0;
  while (disallowed_cpu < 6 &&
         ((DWORD_PTR(1) << disallowed_cpu) & affinity_guard.restricted_mask) != 0) {
    ++disallowed_cpu;
  }
  Check(disallowed_cpu < 6, "test mask unexpectedly covers all six guest CPUs");
  guest_threads[0]->SetActiveCpu(disallowed_cpu);
  Check(ThreadAffinityWithin(guest_threads[0]->thread()->system_id(),
                             affinity_guard.restricted_mask),
        "explicit affinity pinned a CPU outside the process mask");
  Check(sink->warning_count.load() == 1,
        "the explicit-affinity path emitted an extra guest CPU affinity warning");
  Check(rex::cvar::SetFlagByName("ignore_thread_affinities", "true"),
        "could not restore ignore_thread_affinities");

  Check(sink->last_warning.find("available to this process") != std::string::npos,
        "warning does not report the processors available to the process");
  Check(sink->last_warning.find("ignore_thread_affinities=") != std::string::npos,
        "warning does not report the affinity delegation policy");

  guest_threads.clear();

  std::printf("PASS: one warning, guest state preserved, process mask respected\n");
  return 0;
}

