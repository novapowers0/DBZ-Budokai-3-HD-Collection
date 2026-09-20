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

# The v0.10.0 release header misses the virtual dispatch point used by the
# dual-region app. Keep the override valid so EU images select their mappings.
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
