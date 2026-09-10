import sys, os, glob, re

# Post-process generated code to fix bctr sites that dispatch through a
# function-pointer/vtable table but were mis-detected as a 1-entry jump table.
# The generated code then reads:
#
#     // bctr 
#     switch (ctx.r11.u32) {
#     case 0:
#         <target>(ctx, base);
#         return;
#     default:
#         __builtin_trap(); // Switch case out of range
#     }
#
# Any real table entry other than the first hits __builtin_trap() which faults
# with 0xC000001D. Replace it with a plain indirect call.
#
# Known instances (EU): sub_820F2370 (bctr 0x820F2390, table 0x8201E348),
# sub_820BB8C8 (vtable 0x82122B08). The US core can have the same shape.
#
# Re-run ALWAYS after re-codegen: the recompiler regenerates the trap and the
# `indirect_calls` TOML hint is a no-op in SDK 0.10.0.
#
# NOTE: this script is format-tolerant on purpose. It matches the 7-line block
# by shape (not by an exact regex) so the recompiler's symbol prefix
# (`sub_`, `dbz3eu_sub_`, `dbz3_sub_`) and trailing comment space don't break it.

FUNC_RE = re.compile(r"DEFINE_REX_FUNC\((\w+)\)")


def is_block(lines, i):
    """Return the map register if lines[i..i+7] are a single-case bctr trap."""
    if i + 7 >= len(lines):
        return None
    l0 = lines[i]
    l1 = lines[i + 1]
    l2 = lines[i + 2]
    l3 = lines[i + 3]
    l4 = lines[i + 4]
    l5 = lines[i + 5]
    l6 = lines[i + 6]
    l7 = lines[i + 7]
    if not re.match(r"^\t// bctr\s*$", l0):
        return None
    m = re.match(r"^\tswitch \(ctx\.(\w+)\.u32\) \{$", l1)
    if not m:
        return None
    if l2 != "\tcase 0:":
        return None
    if not re.match(r"^\t\t(goto loc_[0-9A-Fa-f]{8}|[\w]+_sub_[0-9A-Fa-f]{8}\(ctx, base\);|[\w]+\(ctx, base\);)$", l3):
        return None
    if l4 != "\t\treturn;":
        return None
    if l5 != "\tdefault:":
        return None
    if l6 != "\t\t__builtin_trap(); // Switch case out of range":
        return None
    if l7 != "\t}":
        return None
    return m.group(1)


def patch_file(path, apply):
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    sites = []
    i = 0
    while i < len(lines):
        reg = is_block(lines, i)
        if reg is not None:
            fn = "?"
            for j in range(i, -1, -1):
                m = FUNC_RE.search(lines[j])
                if m:
                    fn = m.group(1)
                    break
            sites.append((i, fn, reg))
            i += 8
            continue
        i += 1
    for (idx, fn, reg) in sites:
        print("  %s:%d  %s (bctr via %s)" % (os.path.basename(path), idx + 1, fn, reg))
    if apply and sites:
        for (idx, fn, reg) in sorted(sites, reverse=True):
            lines[idx:idx + 8] = [
                "\t// bctr (function-pointer table dispatch; NOT a jump table)",
                "\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);",
                "\treturn;",
            ]
        open(path, "w", encoding="utf-8", newline="").write("\n".join(lines))
        print("  PATCHED %s (%d site(s))" % (os.path.basename(path), len(sites)))
    return len(sites)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    apply = "--apply" in sys.argv
    if not args:
        print("usage: fix_eu_bctr.py [--apply] <codegen_dir> [more dirs...]")
        print("  detects/ fixes single-case bctr traps (0xC000001D) in")
        print("  dbz3_recomp.*.cpp and dbz3_eu_recomp.*.cpp")
        return 1
    total = 0
    changed = 0
    for d in args:
        for patt in ("dbz3_recomp.*.cpp", "dbz3_eu_recomp.*.cpp"):
            for p in sorted(glob.glob(os.path.join(d, patt))):
                total += patch_file(p, apply)
                changed += 1
    if total:
        if apply:
            print("OK patched %d site(s)" % total)
        else:
            print("FOUND %d site(s) (dry run; pass --apply to fix)" % total)
    else:
        print("NO PATCH (pattern not found) - codegen is clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
