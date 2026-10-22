#pragma once
#include <cstdint>
namespace dbzlab {
void worldObserve(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base) noexcept;
void worldPresent() noexcept;
void worldShutdown() noexcept;
}
