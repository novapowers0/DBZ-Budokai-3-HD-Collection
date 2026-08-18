// fix_config - Loads XEX, finds real sizes for the rex_sub_* stubs
#include <rex/runtime.h>
#include <rex/logging.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

// The addresses currently declared with size=0x10 (truncated) or 0x08
static const uint32_t kTargets[] = {
    0x82097F08, 0x820F8D28, 0x8213EA00, 0x821DEED0, 0x821DE85C,
    0x821EDCE0, 0x821EE810, 0x82292C50, 0x82292C68, 0x82296528,
    0x8228D8B0, 0x821EDCF0, 0x82294030, 0x821FB170, 0x82294788,
    0x8228D890, 0x8215B3B8, 0x820BB1E8,
};

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: fix_config <xex_path>\n"); return 1; }
    std::filesystem::path xex_path = argv[1];
    std::filesystem::path game_dir = xex_path.parent_path();

    auto log_config = rex::BuildLogConfig(nullptr, "warn", {});
    rex::InitLogging(log_config);
    rex::Runtime runtime(game_dir);
    rex::RuntimeConfig config;
    config.tool_mode = true;
    rex::X_STATUS status = runtime.Setup(std::move(config));
    if (XFAILED(status)) { printf("setup failed %08X\n", status); return 1; }
    status = runtime.LoadXexImage("game:\\default.xex");
    if (XFAILED(status)) { printf("load failed %08X\n", status); return 1; }
    uint8_t* base = runtime.virtual_membase();

    // For each target, scan forward for the next valid function entry point.
    // A function boundary is where the bytes look like a PPC prologue or a
    // known function start. We approximate: scan the code range (0x82080000+)
    // and find the next address that is a registered function entry.
    // Simpler: just print the gap to the next known entry by scanning for the
    // next 0x4D..... (bl) or prologue pattern. For now, print code bytes at
    // each target so we can see the real function extent.

    printf("Dumping code at each target (0x40 bytes = 16 instructions):\n");
    for (uint32_t t : kTargets) {
        printf("\n0x%08X:\n", t);
        for (int i = 0; i < 0x40; i += 4) {
            uint32_t v; std::memcpy(&v, base + t + i, 4);
            printf("  +0x%02X: %08X\n", i, __builtin_bswap32(v));
        }
    }
    runtime.Shutdown();
    return 0;
}
