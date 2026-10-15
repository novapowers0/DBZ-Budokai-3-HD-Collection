#pragma once
#include <cstdint>
namespace dbzlab {
// Configure once during UI setup, before launching the guest draw thread.
void streamSetPresentationWindow(void* window);
using StreamMemoryArm=void(*)(void*,uint32_t,uint32_t);
using StreamMemoryEpoch=uint64_t(*)(void*,uint32_t,uint32_t);
void streamSetMemoryWatch(void* context,StreamMemoryArm arm,StreamMemoryEpoch epoch);
void streamObserve(uint32_t device,uint32_t primitive,int32_t vertexBase,uint32_t indexStart,uint32_t count,uint8_t* base) noexcept;
void streamObserveUp(uint32_t device,uint32_t primitive,uint32_t count,uint32_t vertices,uint32_t stride,uint8_t* base) noexcept;
void streamResolve(uint32_t device,uint32_t flags,uint32_t destination,uint8_t* base,uint32_t source_rect=0,uint32_t destination_point=0) noexcept;
void streamPresent() noexcept;
void streamShutdown() noexcept;
void streamStatus() noexcept;
bool streamCapture(const char* path) noexcept;
}
