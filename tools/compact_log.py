#!/usr/bin/env python3
"""
Compact log analyzer for rexglue recompiled projects.
Filters out noise (texture fetch spam) and shows a timeline of key events.
Usage: python compact_log.py <path-to-log> [--show-trace] [--since HH:MM:SS]
"""
import re
import sys
import argparse
from collections import Counter

NOISE_PATTERNS = [
    r'Texture fetch constant',
]

# Order matters: show these categories preferentially
KEY_PATTERNS = [
    # Crash / errors
    r'UNHANDLED EXCEPTION|\[FATAL\]|\[critical\]',
    r'Call to invalid or unregistered function',
    r'ResolveFunction|unresolved',
    # Audio
    r'\[apu\]|XMA|Xma|ffmpeg|avcodec|Audio|audio|XAudio|RegisterRender|SubmitRender|SDL.*device|OpenAudio|speaker',
    # File system
    r'NtCreateFile|CreateFile|\.afs|\.sfd|\.xex|Mounted|VFS|missing|not found|FAILED',
    # GPU / graphics
    r'\[gpu\].*(pipeline|shader|render|swap|present|surface|vsync|framebuffer)',
    r'SetInterruptCallback|VSync|Interrupt',
    # Kernel / system
    r'\[krnl\].*(XMA|audio|XAudio)',
    r'KernelState|LaunchModule|module',
    # Input
    r'ControllerDeviceAdded|ControllerDeviceRemoved',
]

def is_noise(line):
    for p in NOISE_PATTERNS:
        if re.search(p, line, re.IGNORECASE):
            return True
    return False

def is_key(line):
    for p in KEY_PATTERNS:
        if re.search(p, line, re.IGNORECASE):
            return True
    return False

def parse_ts(line):
    m = re.match(r'\[(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2}\.\d+)\]', line)
    if m:
        return m.group(2)
    return None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('logfile')
    parser.add_argument('--show-trace', action='store_true', help='Show all non-noise lines including trace/debug')
    parser.add_argument('--since', help='Only show lines at/after this timestamp HH:MM:SS')
    parser.add_argument('--until', help='Only show lines before this timestamp HH:MM:SS')
    parser.add_argument('--summary', action='store_true', help='Print category summary only')
    args = parser.parse_args()

    with open(args.logfile, encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    total = len(lines)
    kept = []
    cat_counts = Counter()
    crash_lines = []

    for line in lines:
        line = line.rstrip('\n')
        if is_noise(line):
            continue
        ts = parse_ts(line)
        if args.since and ts and ts < args.since:
            continue
        if args.until and ts and ts >= args.until:
            continue
        # Categorize
        if '[apu]' in line:
            cat_counts['APU/audio'] += 1
        elif '[gpu]' in line:
            cat_counts['GPU'] += 1
        elif '[krnl]' in line:
            cat_counts['Kernel'] += 1
        elif '[fs]' in line:
            cat_counts['Filesystem'] += 1
        elif '[sys]' in line:
            cat_counts['System'] += 1
        elif 'UNHANDLED EXCEPTION' in line or '[FATAL]' in line or '[critical]' in line:
            cat_counts['CRASH'] += 1
            crash_lines.append(line)
        elif '[core]' in line:
            cat_counts['Core'] += 1
        kept.append(line)

    print(f"=== LOG SUMMARY: {total} raw lines, {len(kept)} meaningful (filtered {total-len(kept)} noise) ===")
    print("\n--- Category counts (meaningful lines) ---")
    for cat, cnt in cat_counts.most_common():
        print(f"  {cat:12s}: {cnt}")

    if crash_lines:
        print("\n--- CRASH ---")
        for cl in crash_lines:
            print(f"  {cl}")

    if args.summary:
        return

    print(f"\n--- Timeline ({len(kept)} lines) ---")
    for line in kept:
        if is_key(line) or args.show_trace:
            print(f"  {line}")

if __name__ == '__main__':
    main()
