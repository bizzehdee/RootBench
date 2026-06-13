#pragma once
// High-performance double-buffered video blit engine (plan §22).
// All TUI drawing targets the shadow buffer in system RAM; Present() flushes
// only the dirty pixel-row range to the hardware GOP framebuffer using
// AVX2 non-temporal streaming stores (falls back to memcpy when AVX2 is
// absent or not yet enabled via CpuFeatures::EnableAvxState()).

#include "UefiTypes.h"

struct FrameBufferTopology {
    UINT32* HardwareBaseAddress;   // GOP framebuffer base (write destination)
    UINT32* ShadowBuffer;          // System-RAM back-buffer  (read source)
    UINT32  HorizontalResolution;
    UINT32  VerticalResolution;
    UINT32  PixelsPerScanLine;
};

namespace VideoEngine {

// Bind the engine to the renderer's framebuffer state.
// Must be called after Renderer::Init() and after each SetModeByIndex().
void Setup(UINT32* hwFb, UINT32* shadow,
           UINT32 width, UINT32 height, UINT32 pitch);

// Expand the dirty pixel-row range to cover [startRow, endRow).
// Coordinates are pixel rows (not character-grid rows).
void MarkDirty(UINT32 startRow, UINT32 endRow);

// Blit all dirty rows from ShadowBuffer → HardwareBaseAddress.
// Uses _mm256_loadu_si256 + _mm256_stream_si256 when AVX2 is active,
// memcpy otherwise.
void Present();

// Clear dirty state without flushing (call after mode change / init).
void Reset();

// True if the AVX2 streaming-store path is currently active.
bool IsAvx2Active();

} // namespace VideoEngine
