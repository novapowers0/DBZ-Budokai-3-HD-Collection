// find_jtables - Loads the XEX in tool mode and scans for jump tables
// A jump table is a run of 4-byte entries all within the code range 0x82080000-0x8230EC00
#include <rex/runtime.h>
#include <rex/logging.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: find_jtables <xex_path>\n");
        return 1;
    }
    std::filesystem::path xex_path = argv[1];
    std::filesystem::path game_dir = xex_path.parent_path();

    auto log_config = rex::BuildLogConfig(nullptr, "warn", {});
    rex::InitLogging(log_config);

    rex::Runtime runtime(game_dir);
    rex::RuntimeConfig config;
    config.tool_mode = true;
    rex::X_STATUS status = runtime.Setup(std::move(config));
    if (XFAILED(status)) {
        printf("Runtime setup failed: %08X\n", status);
        return 1;
    }
    status = runtime.LoadXexImage("game:\\default.xex");
    if (XFAILED(status)) {
        printf("LoadXexImage failed: %08X\n", status);
        return 1;
    }
    uint8_t* base = runtime.virtual_membase();
    if (!base) {
        printf("No membase\n");
        return 1;
    }

    // Scan the data region (0x82000000 - 0x82080000) and code for runs of
    // valid guest addresses (0x82080000-0x8230EC00) of length >= 4.
    const uint32_t start = 0x82000000;
    const uint32_t end   = 0x8230EC00;
    printf("Scanning for jump tables...\n");
    int table_count = 0;
    for (uint32_t addr = start; addr < end - 16; addr += 4) {
        // check 4 consecutive entries are valid code addresses
        bool ok = true;
        for (int i = 0; i < 4; i++) {
            uint32_t val;
            std::memcpy(&val, base + addr + i*4, 4);
            uint32_t be = __builtin_bswap32(val);
            if (be < 0x82080000 || be >= 0x8230EC00) { ok = false; break; }
        }
        if (ok) {
            // count the run length
            int len = 4;
            while (len < 256) {
                uint32_t val;
                std::memcpy(&val, base + addr + len*4, 4);
                uint32_t be = __builtin_bswap32(val);
                if (be < 0x82080000 || be >= 0x8230EC00) break;
                len++;
            }
            printf("Jump table @ 0x%08X : %d entries\n", addr, len);
            // print first 8 entries
            for (int i = 0; i < len && i < 8; i++) {
                uint32_t val;
                std::memcpy(&val, base + addr + i*4, 4);
                printf("  [%d] = 0x%08X\n", i, __builtin_bswap32(val));
            }
            if (len > 8) printf("  ... (%d more)\n", len - 8);
            table_count++;
            addr += len*4;  // skip past
        }
    }
    printf("Total jump tables: %d\n", table_count);
    runtime.Shutdown();
    return 0;
}
