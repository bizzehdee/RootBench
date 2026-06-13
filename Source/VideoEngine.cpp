// High-performance double-buffered video blit engine (plan §22).
// Shadow buffer (system RAM) → hardware GOP framebuffer blit using
// AVX2 non-temporal streaming stores to maximise PCIe write throughput
// and avoid polluting CPU caches with framebuffer data.
//
// The AVX2 path uses _mm256_loadu_si256 (unaligned load from shadow buffer,
// which AllocatePool aligns to 8 bytes) and _mm256_stream_si256 (aligned
// non-temporal store to the GOP framebuffer, which is always page-aligned).
// A runtime guard falls back to memcpy if AVX2 is absent or not yet enabled.

#include "VideoEngine.h"
#include "CpuFeatures.h"
#include "Freestanding.h"
#include <immintrin.h>

namespace VideoEngine {

// ── State ─────────────────────────────────────────────────────
static FrameBufferTopology sTopo         = {};
static UINT32              sDirtyStart   = 0;   // inclusive pixel row
static UINT32              sDirtyEnd     = 0;   // exclusive pixel row
static bool                sInitialized  = false;

// ── AVX2 streaming blit ───────────────────────────────────────
// Tagged so the compiler emits AVX2 for this function even without a
// global -mavx2 flag (matching the pattern used by FpVectorBenchmark).
// dst must be 32-byte aligned — GOP framebuffers are page-aligned and
// standard scanline widths (800, 1024, 1920 … ×4 bytes) are multiples
// of 32, so every row-start address is 32-byte aligned in practice.
__attribute__((target("avx2")))
static void BltAvx2(UINT8* dst, const UINT8* src, UINTN bytes) {
    // Bail to scalar if dst somehow lacks the required 32-byte alignment.
    if (reinterpret_cast<UINTN>(dst) & 31u) {
        memcpy(dst, src, bytes);
        return;
    }
    UINTN blocks = bytes / 32;
    for (UINTN i = 0; i < blocks; ++i) {
        // Unaligned load — shadow buffer has only 8-byte alignment guarantee
        __m256i v = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src + i * 32));
        // Non-temporal store — bypasses the CPU cache hierarchy so we do not
        // evict useful TUI data when pushing megabytes of pixel data
        _mm256_stream_si256(
            reinterpret_cast<__m256i*>(dst + i * 32), v);
    }
    // Scalar tail for any sub-32-byte remainder at the end of the region
    UINTN tail = blocks * 32;
    for (UINTN j = tail; j < bytes; ++j)
        dst[j] = src[j];
    // Ensure all non-temporal stores reach the PCIe device before we return
    _mm_sfence();
}

// ── Public API ────────────────────────────────────────────────

void Setup(UINT32* hwFb, UINT32* shadow,
           UINT32 width, UINT32 height, UINT32 pitch) {
    sTopo.HardwareBaseAddress  = hwFb;
    sTopo.ShadowBuffer         = shadow;
    sTopo.HorizontalResolution = width;
    sTopo.VerticalResolution   = height;
    sTopo.PixelsPerScanLine    = pitch;
    sInitialized = (hwFb && shadow && width && height && pitch);
    // Nothing dirty yet — caller will immediately draw and Present()
    sDirtyStart = height;
    sDirtyEnd   = 0;
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

    UINTN pitch  = sTopo.PixelsPerScanLine;
    UINTN offset = static_cast<UINTN>(start) * pitch * sizeof(UINT32);
    UINTN bytes  = static_cast<UINTN>(end - start) * pitch * sizeof(UINT32);

    UINT8*       dst = reinterpret_cast<UINT8*>(sTopo.HardwareBaseAddress) + offset;
    const UINT8* src = reinterpret_cast<const UINT8*>(sTopo.ShadowBuffer)  + offset;

    if (IsAvx2Active()) {
        BltAvx2(dst, src, bytes);
    } else {
        memcpy(dst, src, bytes);
    }

    // Reset dirty range — engine is clean until next MarkDirty() call
    sDirtyStart = sTopo.VerticalResolution;
    sDirtyEnd   = 0;
}

void Reset() {
    sDirtyStart = sTopo.VerticalResolution;
    sDirtyEnd   = 0;
}

bool IsAvx2Active() {
    return sInitialized
        && CpuFeatures::IsAvxEnabled()
        && CpuFeatures::Get().HasAVX2;
}

} // namespace VideoEngine
