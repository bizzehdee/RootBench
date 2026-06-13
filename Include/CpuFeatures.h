#pragma once
// CPU feature detection and AVX/AESNI state enablement.
// Call Detect() once on BSP during startup; call EnableAvxState() on each CPU
// (BSP and APs) before executing any AVX/AVX2/FMA instructions.

#include "UefiTypes.h"

namespace CpuFeatures {

struct Features {
    bool HasSSE2;
    bool HasSSE42;
    bool HasAVX;
    bool HasAVX2;
    bool HasFMA;
    bool HasAESNI;
    bool HasSHA;
    bool HasXSave;
    bool HasAVX512F;    // AVX-512 Foundation (leaf 7 sl0 EBX[16])
    bool HasAVX512VNNI; // AVX-512 Vector Neural Network (leaf 7 sl0 ECX[11])
    bool HasAVXVNNI;    // AVX-VNNI (leaf 7 sl1 EAX[4])
};

// Detect features via CPUID. Safe to call before EnableAvxState().
void Detect();

// Returns the feature set detected by Detect().
const Features& Get();

// Enable AVX state on the current CPU core:
//   1. Sets CR4.OSFXSR / OSXMMEXCPT / OSXSAVE
//   2. Calls XSETBV to enable x87 + SSE + AVX in XCR0
// Must be called on every core (BSP and each AP) that will run AVX code.
// Returns true on success; false if the CPU lacks XSAVE or AVX support.
bool EnableAvxState();

// Returns true if EnableAvxState() has been successfully called on this BSP.
// Used by VideoEngine to decide whether the AVX2 blit path is safe to execute.
bool IsAvxEnabled();

} // namespace CpuFeatures
