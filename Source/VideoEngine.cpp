// Double-buffered video blit engine.
// Render mode is selected at compile time via VIDEO_BLT (see VideoEngine.h).

#include "VideoEngine.h"
#include "CpuFeatures.h"
#include "Freestanding.h"
// Force the AVX/AVX2 feature macros on before including immintrin.h so that
// avxintrin.h is not skipped by its #ifndef __AVX__ guard.  These macros
// only control which declarations the header exposes — they do NOT make the
// compiler emit AVX2 instructions in ordinary functions.  The BltAvx2 path
// is runtime-gated by CpuFeatures::HasAVX2 and carries its own
// __attribute__((target("avx,avx2"))) for actual code-gen.
#ifndef __AVX__
#  define __AVX__  1
#endif
#ifndef __AVX2__
#  define __AVX2__ 1
#endif
#include <immintrin.h>

namespace VideoEngine {

// ── State ─────────────────────────────────────────────────────
static FrameBufferTopology           sTopo        = {};
static EFI_GRAPHICS_OUTPUT_PROTOCOL* sGop         = nullptr;
static UINT32                        sDirtyStart  = 0;
static UINT32                        sDirtyEnd    = 0;
static bool                          sInitialized = false;

// ── AVX2 streaming blit ───────────────────────────────────────
// dst must be 32-byte aligned — GOP framebuffers are page-aligned and
// standard scanline widths (800, 1024, 1920 … ×4 bytes) are multiples
// of 32, so every row-start is 32-byte aligned in practice.
__attribute__((target("avx,avx2")))
static void BltAvx2(UINT8* dst, const UINT8* src, UINTN bytes) {
    // 1. Structural Alignment Guard
    if (reinterpret_cast<UINTN>(dst) & 31u) {
        optimized_memcpy(dst, src, bytes);
        return;
    }

    // Cast directly to the target types to eliminate manual multiplications in the loop
    auto* d256 = reinterpret_cast<__m256i*>(dst);
    auto* s256 = reinterpret_cast<const __m256i*>(src);

    // 2. Unroll by 4 blocks (128 bytes per loop iteration)
    UINTN blocks_128 = bytes / 128;
    for (UINTN i = 0; i < blocks_128; ++i) {
        // Queue up multiple unaligned loads back-to-back. 
        // This lets the CPU pipelining architecture overlap memory fetch latencies.
        __m256i v0 = _mm256_loadu_si256(s256 + 0);
        __m256i v1 = _mm256_loadu_si256(s256 + 1);
        __m256i v2 = _mm256_loadu_si256(s256 + 2);
        __m256i v3 = _mm256_loadu_si256(s256 + 3);

        // Stream them to the cache-bypassing controller sequentially
        _mm256_stream_si256(d256 + 0, v0);
        _mm256_stream_si256(d256 + 1, v1);
        _mm256_stream_si256(d256 + 2, v2);
        _mm256_stream_si256(d256 + 3, v3);

        s256 += 4;
        d256 += 4;
    }

    // 3. Handle the remaining 32-byte chunks (0 to 3 blocks left over)
    UINTN remaining_bytes = bytes % 128;
    UINTN blocks_32 = remaining_bytes / 32;
    for (UINTN i = 0; i < blocks_32; ++i) {
        __m256i v = _mm256_loadu_si256(s256);
        _mm256_stream_si256(d256, v);
        s256++;
        d256++;
    }

    // 4. Optimized Tail: Drop back to our 64-bit word fallback, not an 8-bit loop
    remaining_bytes %= 32;
    if (remaining_bytes > 0) {
        auto* d_tail = reinterpret_cast<UINT8*>(d256);
        auto* s_tail = reinterpret_cast<const UINT8*>(s256);
        optimized_memcpy(d_tail, s_tail, remaining_bytes);
    }

    // 5. Commit non-temporal buffers to physical memory
    _mm_sfence();
}

// ── Public API ────────────────────────────────────────────────

void Setup(UINT32* hwFb, UINT32* shadow,
           UINT32 width, UINT32 height, UINT32 pitch,
           EFI_GRAPHICS_OUTPUT_PROTOCOL* gop) {
    sTopo.HardwareBaseAddress  = hwFb;
    sTopo.ShadowBuffer         = shadow;
    sTopo.HorizontalResolution = width;
    sTopo.VerticalResolution   = height;
    sTopo.PixelsPerScanLine    = pitch;
    sGop         = gop;
    sInitialized = (hwFb && shadow && width && height && pitch);
    sDirtyStart  = height;
    sDirtyEnd    = 0;
}

void MarkDirty(UINT32 startRow, UINT32 endRow) {
    if (startRow < sDirtyStart) sDirtyStart = startRow;
    if (endRow   > sDirtyEnd  ) sDirtyEnd   = endRow;
}

void Present() {
    if (!sInitialized) return;
    if (sDirtyStart >= sDirtyEnd) return;

    UINT32 start = sDirtyStart;
    UINT32 end   = (sDirtyEnd < sTopo.VerticalResolution)
                   ? sDirtyEnd : sTopo.VerticalResolution;

#if VIDEO_BLT
    // ── GOP Blt path ──────────────────────────────────────────
    // Shadow buffer is always in EFI_GRAPHICS_OUTPUT_BLT_PIXEL (BGRA) order
    // (enforced by ToPixel() in Renderer.cpp when VIDEO_BLT=1). GOP->Blt()
    // handles any conversion to the actual hardware pixel format internally.
    // Delta = bytes-per-row in the source buffer, accounting for pitch padding.
    if (sGop) {
        sGop->Blt(sGop,
                  reinterpret_cast<EFI_GRAPHICS_OUTPUT_BLT_PIXEL*>(
                      sTopo.ShadowBuffer),
                  EfiBltBufferToVideo,
                  0, start,                            // src  X, Y
                  0, start,                            // dest X, Y
                  sTopo.HorizontalResolution,
                  end - start,
                  sTopo.PixelsPerScanLine * sizeof(UINT32));
        sDirtyStart = sTopo.VerticalResolution;
        sDirtyEnd   = 0;
        return;
    }
    // GOP pointer absent — fall through to direct MMIO as a safety net.
#endif

    // ── Direct MMIO path (AVX2 non-temporal / memcpy) ─────────
    {
        UINTN pitch  = sTopo.PixelsPerScanLine;
        UINTN offset = static_cast<UINTN>(start) * pitch * sizeof(UINT32);
        UINTN bytes  = static_cast<UINTN>(end - start) * pitch * sizeof(UINT32);

        UINT8*       dst = reinterpret_cast<UINT8*>(sTopo.HardwareBaseAddress) + offset;
        const UINT8* src = reinterpret_cast<const UINT8*>(sTopo.ShadowBuffer)  + offset;

        if (CpuFeatures::IsAvxEnabled() && CpuFeatures::Get().HasAVX2)
            BltAvx2(dst, src, bytes);
        else
            optimized_memcpy(dst, src, bytes);
    }

    sDirtyStart = sTopo.VerticalResolution;
    sDirtyEnd   = 0;
}

void Reset() {
    sDirtyStart = sTopo.VerticalResolution;
    sDirtyEnd   = 0;
}

RenderMode GetRenderMode() {
#if VIDEO_BLT
    return sGop ? RenderMode::GopBlt : RenderMode::Avx2;
#else
    return (CpuFeatures::IsAvxEnabled() && CpuFeatures::Get().HasAVX2)
           ? RenderMode::Avx2 : RenderMode::Memcpy;
#endif
}

} // namespace VideoEngine
