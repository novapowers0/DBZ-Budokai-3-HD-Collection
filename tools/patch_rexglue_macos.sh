#!/usr/bin/env bash
set -euo pipefail

# macOS (Apple Silicon, Vulkan via MoltenVK): run after copying patches/rexglue-sdk
# over a ReXGlue v0.10.0 checkout, like tools/patch_rexglue_linux.sh on Linux.
# Usage: bash tools/patch_rexglue_macos.sh [rexglue-sdk-0.10]
sdk="${1:-rexglue-sdk-0.10}"
here="$(cd "$(dirname "$0")" && pwd)"
chrono="$sdk/include/rex/chrono/chrono.h"

# The Linux script adds clock_time_conversion/clock_cast for libstdc++. On macOS
# chrono.h already defines them in its __APPLE__ block (a second definition does
# not compile), so mark that step as done and reuse the rest of the script (the
# Vulkan presenter frame cap/FIFO fix applies to MoltenVK too).
if ! grep -q 'REXGLUE_LINUX_CLOCK_TIME_CONVERSION' "$chrono"; then
  printf '\n// REXGLUE_LINUX_CLOCK_TIME_CONVERSION: macOS defines clock_cast in the __APPLE__ block above.\n' >> "$chrono"
fi
bash "$here/patch_rexglue_linux.sh" "$chrono"

# Vulkan presenter: on MoltenVK a swapchain created with oldSwapchain (the smooth
# reconnection path) handed Metal 1x1 drawables - Metal API validation:
# "renderTargetWidth must be <= minimum attachment width (1)" - so after any
# window resize or fullscreen toggle every frame was lost (black screen or stale
# launcher images). Reconnect from scratch instead, like the first connection.
vulkan_presenter="$sdk/src/ui/vulkan/vulkan_presenter.cpp"
if ! grep -q 'REXGLUE_DBZ3_MAC_SWAPCHAIN_RECREATE' "$vulkan_presenter"; then
python3 - "$vulkan_presenter" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
needle = '''  if (paint_context_.vulkan_surface != VK_NULL_HANDLE) {
    VkSwapchainKHR old_swapchain = paint_context_.PrepareForSwapchainRetirement();'''
insert = '''#if REX_PLATFORM_MAC
  // REXGLUE_DBZ3_MAC_SWAPCHAIN_RECREATE: MoltenVK + oldSwapchain gave Metal 1x1
  // drawables after a resize (every frame lost). Start from scratch instead.
  paint_context_.DestroySwapchainAndVulkanSurface();
#endif
'''
if text.count(needle) != 1:
    raise SystemExit('Vulkan presenter reconnection marker not found')
path.write_text(text.replace(needle, insert + needle, 1))
PY
fi
grep -q 'REXGLUE_DBZ3_MAC_SWAPCHAIN_RECREATE' "$vulkan_presenter"
