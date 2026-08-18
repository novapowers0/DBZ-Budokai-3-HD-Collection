// extract_jt - Loads the XEX in tool mode and dumps the jump table at 0x8201E350
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: extract_jt <xex_path>\n");
        return 1;
    }
    std::filesystem::path xex_path = argv[1];
    std::filesystem::path game_dir = xex_path.parent_path();

    auto log_config = rex::BuildLogConfig(nullptr, "info", {});
    rex::InitLogging(log_config);

    rex::Runtime runtime(game_dir);
    rex::RuntimeConfig config;
    config.tool_mode = true;
    rex::X_STATUS status = runtime.Setup(std::move(config));
    if (XFAILED(status)) {
        printf("Runtime setup failed: %08X\n", status);
        return 1;
    }

    status = runtime.LoadXexImage("game:\\default.xex");    if (XFAILED(status)) {
        printf("LoadXexImage failed: %08X\n", status);
        return 1;
    }

    uint8_t* base = runtime.virtual_membase();
    if (!base) {
        printf("No membase\n");
        return 1;
    }

    printf("membase=%p\n", base);
    // Jump table at guest 0x82322B08 (sub_820BB938, lis -32206 -> 0x82320000 + addi 11016).
    const uint32_t jt = 0x82322B08;
    printf("Jump table at 0x%08X:\n", jt);
    for (uint32_t off = 0; off < 0x200; off += 4) {
        uint32_t val;
        std::memcpy(&val, base + jt + off, 4);
        uint32_t be = __builtin_bswap32(val);
        printf("  0x%08X: 0x%08X\n", jt + off, be);
    }

    runtime.Shutdown();
    return 0;
}
