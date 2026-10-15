#pragma once
#include <cstdint>
struct PPCContext;
namespace dbzlab {
void shadowObserve(PPCContext& ctx,uint8_t* base) noexcept;
void shadowPresent() noexcept;
void shadowShutdown() noexcept;
}
