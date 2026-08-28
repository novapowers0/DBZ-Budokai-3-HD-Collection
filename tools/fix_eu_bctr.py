import sys, os, glob, re

# Post-process the generated code to fix wrong jump-table classifications at
# bctr sites that actually dispatch to a function-pointer/vtable table.
#
# The auto-detector reads a function-pointer table as a jump table with a
# single case 0 (the first table entry); any other real pointer hits
# __builtin_trap() -> 0xC000001D. Known instances:
#   sub_820F2370 (bctr 0x820F2390, table 0x8201E348) - EU demo battle
#   sub_820BB8C8 (vtable 0x82122B08, virtual call)   - EU demo battle
#   sub_820BB938 (vtable, index *(r3+82)<<2)         - US core (Duel/Start path)
# The pattern below (case 0 with a default trap) is exactly the
# single-case misclassification; multi-case jump tables are left alone.
#
# Re-run ALWAYS after re-codegen: the codegen regenerates the trap.

_SINGLE_CASE = re.compile(
    r"\t// bctr[^\n]*\n"
    r"\tswitch \(ctx\.r11\.u32\) \{\n"
    r"\tcase 0:\n"
    r"\t\t(goto loc_[0-9A-F]{8}|sub_[0-9A-F]{8}\(ctx, base\);\n\t\treturn);\n"
    r"\tdefault:\n"
    r"\t\t__builtin_trap\(\); // Switch case out of range\n"
    r"\t}\n",
    re.DOTALL,
)

_NEW = """\t// bctr (function-pointer table dispatch; NOT a jump table)
\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
\treturn;
"""


def patch_file(path):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    content, n = _SINGLE_CASE.subn(_NEW, content)
    if n:
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print("PATCHED:", os.path.basename(path), f"({n} site(s))")
        return True
    return False


def main():
    if len(sys.argv) < 2:
        print("usage: fix_eu_bctr.py <codegen_dir> [more_codegen_dirs...]")
        print("  processes dbz3_recomp.*.cpp (US) and dbz3_eu_recomp.*.cpp (EU)")
        return 1
    changed = 0
    for d in sys.argv[1:]:
        for patt in ("dbz3_recomp.*.cpp", "dbz3_eu_recomp.*.cpp"):
            for p in glob.glob(os.path.join(d, patt)):
                if patch_file(p):
                    changed += 1
    if changed:
        print("OK patched", changed, "file(s)")
    else:
        print("NO PATCH (pattern not found) - check generated code")
    return 0


if __name__ == "__main__":
    sys.exit(main())