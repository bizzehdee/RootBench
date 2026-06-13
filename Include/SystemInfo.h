#pragma once
// System resource detection: CPU info (CPUID), available memory (GetMemoryMap),
// and MP Services Protocol for multi-core support.

#include "UefiTypes.h"

namespace SystemInfo {

// Call once during early init (after gBS is set).
void Detect();

// CPU information
const char* GetCpuVendor();     // e.g. "GenuineIntel"
const char* GetCpuBrand();      // e.g. "Intel(R) Core(TM) i7-..."
UINT32      GetCpuCoreCount();  // logical processors via CPUID topology
UINT32      GetCpuStepping();   // CPUID leaf 1 EAX[3:0]

// CPU cache sizes (0 if not present or not detectable)
UINT32 GetL1DataCacheKB();
UINT32 GetL1InstCacheKB();
UINT32 GetL2CacheKB();
UINT32 GetL3CacheKB();

// Memory information
UINT64 GetTotalMemoryBytes();   // sum of EfiConventionalMemory regions
UINT64 GetTotalMemoryMB();

// Memory module info from SMBIOS (0 if SMBIOS not available)
UINT32 GetMemorySpeedMHz();           // DIMM rated speed
UINT32 GetMemoryConfiguredSpeedMHz(); // configured operating speed
UINT32 GetMemoryChannelCount();       // inferred from SMBIOS bank locator strings

// MP Services (multi-core) support
bool HasMpServices();
EFI_MP_SERVICES_PROTOCOL* GetMpServices();
UINT32 GetEnabledProcessorCount();  // includes BSP

} // namespace SystemInfo
