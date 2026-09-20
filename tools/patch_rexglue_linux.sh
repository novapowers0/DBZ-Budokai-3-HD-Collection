#!/usr/bin/env bash
set -euo pipefail

# ReXGlue 0.10 specializes clock_time_conversion before declaring the primary
# template. libstdc++ on Ubuntu 22.04 does not provide this C++20 customization
# point, while libc++ does. Add the portable fallback once before the SDK build.
header="${1:-rexglue-sdk-0.10/include/rex/chrono/chrono.h}"
if grep -q 'REXGLUE_LINUX_CLOCK_TIME_CONVERSION' "$header"; then
  :
else
  python3 - "$header" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
needle = 'namespace std::chrono {\n'
insert = '''namespace std::chrono {

#ifndef REXGLUE_LINUX_CLOCK_TIME_CONVERSION
#define REXGLUE_LINUX_CLOCK_TIME_CONVERSION 1
template <class, class>
struct clock_time_conversion {};

template <class DestClock, class SourceClock, class Duration>
auto clock_cast(const std::chrono::time_point<SourceClock, Duration>& t) {
  return clock_time_conversion<DestClock, SourceClock>{}(t);
}
#endif
'''
if needle not in text:
    raise SystemExit("chrono namespace marker not found")
path.write_text(text.replace(needle, insert, 1))
PY
fi

grep -q 'REXGLUE_LINUX_CLOCK_TIME_CONVERSION' "$header"

numeric="$(dirname "$header")/../string/numeric.h"
if ! grep -q 'REXGLUE_LINUX_FLOAT_FROM_CHARS' "$numeric"; then
python3 - "$numeric" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace('#if REX_PLATFORM_MAC', '#if !REX_PLATFORM_WIN32')
text = text.replace('#if !REX_PLATFORM_WIN32\ntemplate <typename T>\ninline std::from_chars_result portable_float_from_chars',
                    '#if !REX_PLATFORM_WIN32\n#define REXGLUE_LINUX_FLOAT_FROM_CHARS 1\ntemplate <typename T>\ninline std::from_chars_result portable_float_from_chars', 1)
path.write_text(text)
PY
fi

timer="$(dirname "$header")/../../../src/core/timer_queue.cpp"
if ! grep -q 'REXGLUE_LINUX_TIMER_THREAD' "$timer"; then
  python3 - "$timer" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace('#include <forward_list>\n',
                    '#include <forward_list>\n#include <atomic>\n#include <thread>\n', 1)
text = text.replace('namespace rex::thread {\n',
                    'namespace rex::thread {\n\n#define REXGLUE_LINUX_TIMER_THREAD 1\n', 1)
text = text.replace(
    'dispatch_thread_ =\n        std::jthread([this](std::stop_token stop_token) { TimerThreadMain(stop_token); });',
    'dispatch_thread_ = std::thread([this] { TimerThreadMain(); });')
text = text.replace('dispatch_thread_.request_stop();',
                    'stop_requested_.store(true, std::memory_order_release);')
text = text.replace('''    // std::jthread auto-joins on destruction
  }''',
                    '''    if (dispatch_thread_.joinable()) {
      dispatch_thread_.join();
    }
  }''')
text = text.replace('void TimerThreadMain(std::stop_token stop_token) {',
                    'void TimerThreadMain() {')
text = text.replace('while (!stop_token.stop_requested()) {',
                    'while (!stop_requested_.load(std::memory_order_acquire)) {')
text = text.replace('std::jthread dispatch_thread_;',
                    'std::thread dispatch_thread_;\n  std::atomic<bool> stop_requested_{false};')
text = text.replace('std::jthread', 'std::thread')
text = text.replace('std::stop_token stop_token', '')
text = text.replace('stop_token.stop_requested()',
                    'stop_requested_.load(std::memory_order_acquire)')
path.write_text(text)
PY
fi
grep -q 'REXGLUE_LINUX_FLOAT_FROM_CHARS' "$numeric"

# Linux Vulkan presenter: apply the same host frame cap as D3D12 and keep FIFO
# as the safe default for Steam/MangoHud compatibility.
vulkan_presenter="$(dirname "$header")/../../../src/ui/vulkan/vulkan_presenter.cpp"
if ! grep -q 'REXGLUE_DBZ3_VULKAN_PRESENT_FIX' "$vulkan_presenter"; then
python3 - "$vulkan_presenter" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace('#include <cmath>\n', '#include <cmath>\n#include <chrono>\n', 1)
text = text.replace('#include <rex/platform.h>\n', '#include <rex/platform.h>\n#include <rex/thread.h>\n', 1)
text = text.replace(
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_immediate, true, "UI/Vulkan",',
    '// REXGLUE_DBZ3_VULKAN_PRESENT_FIX\n'
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_immediate, false, "UI/Vulkan",', 1)
text = text.replace(
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_mailbox, true, "UI/Vulkan",',
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_mailbox, false, "UI/Vulkan",', 1)
text = text.replace(
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_fifo_relaxed, true, "UI/Vulkan",',
    'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_fifo_relaxed, false, "UI/Vulkan",', 1)
if 'REXCVAR_DEFINE_INT32(frame_cap' not in text:
    marker = 'REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_fifo_relaxed'
    marker_pos = text.find(marker)
    if marker_pos < 0:
        raise SystemExit('Vulkan presenter cvar marker not found')
    end_pos = text.find(');', marker_pos)
    if end_pos < 0:
        raise SystemExit('Vulkan presenter FIFO cvar terminator not found')
    text = text[:end_pos + 2] + '''\nREXCVAR_DEFINE_INT32(frame_cap, 0, "UI/Presenter",\n                     "Maximum host present rate in FPS (0 = uncapped)" );\n''' + text[end_pos + 2:]
text = text.replace(
    '  if (REXCVAR_GET(vulkan_allow_present_mode_immediate) &&',
    '  const bool host_present_cap = int32_t(REXCVAR_GET(frame_cap)) > 0;\n'
    '  if (!host_present_cap && REXCVAR_GET(vulkan_allow_present_mode_immediate) &&', 1)
text = text.replace(
    '  } else if (REXCVAR_GET(vulkan_allow_present_mode_mailbox) &&',
    '  } else if (!host_present_cap && REXCVAR_GET(vulkan_allow_present_mode_mailbox) &&', 1)
marker = 'Presenter::PaintResult VulkanPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {\n'
if 'REXGLUE_DBZ3_VULKAN_FRAME_CAP' not in text:
    if marker not in text:
        raise SystemExit('Vulkan presenter paint marker not found')
    text = text.replace(marker, marker + '''  // REXGLUE_DBZ3_VULKAN_FRAME_CAP\n  if (int32_t frame_cap = REXCVAR_GET(frame_cap); frame_cap > 0) {\n    static std::chrono::steady_clock::time_point last_present_time;\n    const auto now = std::chrono::steady_clock::now();\n    if (last_present_time.time_since_epoch().count() != 0) {\n      const std::chrono::nanoseconds interval(1000000000LL / frame_cap);\n      const auto elapsed = now - last_present_time;\n      if (elapsed < interval) {\n        rex::thread::Sleep(std::chrono::duration_cast<std::chrono::microseconds>(interval - elapsed));\n      }\n    }\n    last_present_time = std::chrono::steady_clock::now();\n  }\n\n''', 1)
path.write_text(text)
PY
fi
grep -q 'REXGLUE_DBZ3_VULKAN_PRESENT_FIX' "$vulkan_presenter"

app_header="$(dirname "$header")/../rex_app.h"
if ! grep -q 'REXGLUE_DUAL_IMAGE_RESOLVER' "$app_header"; then
python3 - "$app_header" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
marker = 'REXGLUE_DUAL_IMAGE_RESOLVER'
needle = '  virtual void OnPreSetup(RuntimeConfig& config) {}\n'
insert = '''  // REXGLUE_DUAL_IMAGE_RESOLVER: allow clients to select a region-specific image.
  virtual const rex::PPCImageInfo& ResolveImageInfo(const PathConfig& paths) const {
    (void)paths;
    return ppc_info_;
  }

'''
if marker not in text:
    if needle not in text:
        raise SystemExit("ReXApp OnPreSetup declaration not found")
    text = text.replace(needle, insert + needle, 1)
    path.write_text(text)

member_marker = 'launch_invoked_'
if member_marker not in text:
    needle = '  std::thread module_thread_;\n'
    replacement = needle + '  std::atomic<bool> launch_invoked_{false};\n'
    if needle not in text:
        raise SystemExit("ReXApp module thread member not found")
    path.write_text(path.read_text().replace(needle, replacement, 1))
PY
fi
grep -q 'REXGLUE_DUAL_IMAGE_RESOLVER' "$app_header"
