#pragma once
// High-performance double-buffered video blit engine.
// Three render modes, selected at compile time via VIDEO_BLT:
//
//   VIDEO_BLT=1 (default): GopBlt — flush dirty rows via GOP->Blt()
//               (Boot Services). Fast on most firmware; BSP-only.
//   VIDEO_BLT=0:           Avx2   — AVX2 non-temporal streaming stores
//               to the mapped framebuffer; falls back to Memcpy when
//               AVX2 is not yet active. Safe to call from APs.

#include "UefiTypes.h"

enum class RenderMode : UINT8 { Memcpy, Avx2, GopBlt };

struct FrameBufferTopology {
    UINT32* HardwareBaseAddress;   // GOP framebuffer base (direct MMIO)
    UINT32* ShadowBuffer;          // System-RAM back-buffer (draw target)
    UINT32  HorizontalResolution;
    UINT32  VerticalResolution;
    UINT32  PixelsPerScanLine;
};

namespace VideoEngine {

// Bind the engine to the renderer's framebuffer state.
// gop must be non-null when VIDEO_BLT=1; ignored otherwise.
// Call after Renderer::Init() and after each SetModeByIndex().
void Setup(UINT32* hwFb, UINT32* shadow,
           UINT32 width, UINT32 height, UINT32 pitch,
           EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr);

// Expand the dirty pixel-row range to cover [startRow, endRow).
// Coordinates are pixel rows (not character-grid rows).
void MarkDirty(UINT32 startRow, UINT32 endRow);

// Flush all dirty rows from ShadowBuffer to the display.
// In GopBlt mode: calls GOP->Blt() (BSP only — not AP-safe).
// In Avx2/Memcpy mode: writes directly to FrameBufferBase (AP-safe).
void Present();

// Clear dirty state without flushing (call after mode change / init).
void Reset();

// Active render mode for this build.
RenderMode GetRenderMode();

} // namespace VideoEngine
