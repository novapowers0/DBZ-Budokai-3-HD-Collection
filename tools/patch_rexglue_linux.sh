#!/usr/bin/env bash
set -euo pipefail

# ReXGlue 0.10 specializes clock_time_conversion before declaring the primary
# template. libstdc++ on Ubuntu 22.04 does not provide this C++20 customization
# point, while libc++ does. Add the portable fallback once before the SDK build.
header="${1:-rexglue-sdk-0.10/include/rex/chrono/chrono.h}"
if grep -q 'template <class, class>.*clock_time_conversion' "$header"; then
  exit 0
fi

python3 - "$header" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
needle = 'namespace std::chrono {\n'
insert = '''namespace std::chrono {

template <class, class>
struct clock_time_conversion {};
'''
if needle not in text:
    raise SystemExit("chrono namespace marker not found")
path.write_text(text.replace(needle, insert, 1))
PY

grep -q 'template <class, class>' "$header"
