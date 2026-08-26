import re, glob, os, sys

DIR = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\generated_eu"
PREFIX = "dbz3eu"

RULES = [
    # guest function names: rex_sub_XXXXXXXX and sub_XXXXXXXX (covers __imp__sub_ too)
    (re.compile(r'(rex_|)(sub_)([0-9A-Fa-f]{8})'),
     lambda m: (f"{PREFIX}_rex_sub_" if m.group(1) else f"{PREFIX}_sub_") + m.group(3)),
    # PPC save/restore register helpers
    (re.compile(r'__savefpr_'), f"{PREFIX}_savefpr_"),
    (re.compile(r'__restfpr_'), f"{PREFIX}_restfpr_"),
    (re.compile(r'__savegprlr_'), f"{PREFIX}_savegprlr_"),
    (re.compile(r'__restgprlr_'), f"{PREFIX}_restgprlr_"),
    (re.compile(r'__savevmx_'), f"{PREFIX}_savevmx_"),
    (re.compile(r'__restvmx_'), f"{PREFIX}_restvmx_"),
    (re.compile(r'\bxstart\b'), f"{PREFIX}_xstart"),
    (re.compile(r'\bPPCImageConfig\b'), "PPCImageConfigEU"),
    (re.compile(r'\bPPCFuncMappings\b'), "PPCFuncMappingsEU"),
]

files_changed = 0
for path in glob.glob(os.path.join(DIR, "*.cpp")) + glob.glob(os.path.join(DIR, "*.h")):
    with open(path, encoding="utf-8", errors="replace") as f:
        s = f.read()
    orig = s
    for pat, repl in RULES:
        s = pat.sub(repl, s)
    if s != orig:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(s)
        files_changed += 1

print(f"archivos modificados: {files_changed}")

# verify: no bare sub_XXXXXXXX / rex_sub_XXXXXXXX / xstart / __save* / __rest* left
leftover = []
for path in glob.glob(os.path.join(DIR, "*.cpp")) + glob.glob(os.path.join(DIR, "*.h")):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if re.search(r'(?<!dbz3eu_)(?<!simde_mm_)\bsub_[0-9A-Fa-f]{8}\b', line):
                leftover.append("sub: " + line.strip()[:80])
            if re.search(r'(?<!dbz3eu_)\brex_sub_[0-9A-Fa-f]{8}\b', line):
                leftover.append("rex_sub: " + line.strip()[:80])
            if re.search(r'(?<!dbz3eu_)\bxstart\b', line):
                leftover.append("xstart: " + line.strip()[:80])
            if re.search(r'__save(fpr|gprlr|vmx)_|\__rest(fpr|gprlr|vmx)_', line) and PREFIX not in line.split('_')[0]:
                leftover.append("sav/rest: " + line.strip()[:80])
print(f"restos sin prefijar: {len(leftover)}")
for x in leftover[:10]:
    print("  ", x)