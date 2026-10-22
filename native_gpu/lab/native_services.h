// High-level guest buffer completion and native display timing. No packet API.
#pragma once
#include <cstdint>
#include <memory>
namespace rex::system {class IGraphicsSystem;}
namespace dbzlab {
bool nativeServicesEnabled();
std::unique_ptr<rex::system::IGraphicsSystem> createNativeServices();
void nativeCompleteBuffer(uint8_t* base,uint32_t label,uint32_t serial,uint32_t cursor);
void nativeWriteDisplayCounter(uint8_t* base,uint32_t label,uint32_t endian,uint32_t begin,uint32_t end);
void nativePaceFrame();
void nativeAuditBuffer(uint8_t* base,uint32_t device,uint32_t begin,uint32_t end,uint32_t label,uint32_t serial,uint32_t cursor);
void nativeServicesStatus();
}
