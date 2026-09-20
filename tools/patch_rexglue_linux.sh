#!/usr/bin/env bash
set -euo pipefail

# ReXGlue 0.10 specializes clock_time_conversion before declaring the primary
# template. libstdc++ on Ubuntu 22.04 does not provide this C++20 customization
# point, while libc++ does. Add the portable fallback once before the SDK build.
header="${1:-rexglue-sdk-0.10/include/rex/chrono/chrono.h}"
if grep -q 'REXGLUE_LINUX_CLOCK_TIME_CONVERSION' "$header"; then
  exit 0
fi

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

timer="$(dirname "$header")/../../src/core/timer_queue.cpp"
if ! grep -q '^#include <thread>' "$timer"; then
  python3 - "$timer" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace('#include <forward_list>\n', '#include <forward_list>\n#include <thread>\n', 1)
path.write_text(text)
PY
fi
grep -q 'REXGLUE_LINUX_FLOAT_FROM_CHARS' "$numeric"
