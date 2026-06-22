// Game Physics N-Body benchmark.
// Per-worker SoA particle data stays in L1. AVX2 inner loop processes 8
// j-particles per i-particle iteration using inverse-square gravity (g/d²,
// no sqrt). Scalar fallback auto-vectorises with -O2.

#include "GamePhysicsBenchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"
#ifndef __SSE3__
#  define __SSE3__  1
#endif
#ifndef __SSE4_1__
#  define __SSE4_1__ 1
#endif
#ifndef __AVX__
#  define __AVX__  1
#endif
#ifndef __AVX2__
#  define __AVX2__ 1
#endif
#ifndef __FMA__
#  define __FMA__ 1
#endif
#include <immintrin.h>

// ── Static storage ────────────────────────────────────────────
float GamePhysicsBenchmark::sPx[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];
float GamePhysicsBenchmark::sPy[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];
float GamePhysicsBenchmark::sPz[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];
float GamePhysicsBenchmark::sVx[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];
float GamePhysicsBenchmark::sVy[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];
float GamePhysicsBenchmark::sVz[GamePhysicsBenchmark::MAX_WORKERS][GamePhysicsBenchmark::N];

static constexpr int kN      = GamePhysicsBenchmark::N;
static constexpr float kGrav = 0.0001f;
static constexpr float kEps  = 0.0004f;   // softening prevents singularity at self
static constexpr float kDt   = 0.001f;
static constexpr float kDamp = 0.9999f;
static constexpr float kBnd  = 2.0f;

// ── Scalar step ───────────────────────────────────────────────

static void StepScalar(float* px, float* py, float* pz,
                       float* vx, float* vy, float* vz)
{
    float fx[kN], fy[kN], fz[kN];
    for (int i = 0; i < kN; ++i) { fx[i] = 0; fy[i] = 0; fz[i] = 0; }

    for (int i = 0; i < kN; ++i) {
        for (int j = 0; j < kN; ++j) {
            float dx = px[j] - px[i];
            float dy = py[j] - py[i];
            float dz = pz[j] - pz[i];
            float d2  = dx*dx + dy*dy + dz*dz + kEps;
            float inv = kGrav / d2;
            fx[i] += inv * dx;
            fy[i] += inv * dy;
            fz[i] += inv * dz;
        }
    }

    for (int i = 0; i < kN; ++i) {
        vx[i] = (vx[i] + fx[i] * kDt) * kDamp;
        vy[i] = (vy[i] + fy[i] * kDt) * kDamp;
        vz[i] = (vz[i] + fz[i] * kDt) * kDamp;
        px[i] += vx[i] * kDt;
        py[i] += vy[i] * kDt;
        pz[i] += vz[i] * kDt;
        if (px[i] >  kBnd) { px[i] =  kBnd; vx[i] = -vx[i]; }
        if (px[i] < -kBnd) { px[i] = -kBnd; vx[i] = -vx[i]; }
        if (py[i] >  kBnd) { py[i] =  kBnd; vy[i] = -vy[i]; }
        if (py[i] < -kBnd) { py[i] = -kBnd; vy[i] = -vy[i]; }
        if (pz[i] >  kBnd) { pz[i] =  kBnd; vz[i] = -vz[i]; }
        if (pz[i] < -kBnd) { pz[i] = -kBnd; vz[i] = -vz[i]; }
    }
}

// ── AVX2/FMA step ─────────────────────────────────────────────

__attribute__((target("avx2,fma")))
static float HorizSum8(__m256 r) {
    __m128 lo = _mm256_castps256_ps128(r);
    __m128 hi = _mm256_extractf128_ps(r, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_hadd_ps(lo, lo);
    lo = _mm_hadd_ps(lo, lo);
    return _mm_cvtss_f32(lo);
}

__attribute__((target("avx2,fma")))
static void StepAvx2(float* px, float* py, float* pz,
                     float* vx, float* vy, float* vz)
{
    static_assert(kN % 8 == 0, "N must be a multiple of 8");

    const __m256 veps  = _mm256_set1_ps(kEps);
    const __m256 vgrav = _mm256_set1_ps(kGrav);

    float fx[kN], fy[kN], fz[kN];
    for (int i = 0; i < kN; ++i) { fx[i] = 0; fy[i] = 0; fz[i] = 0; }

    for (int i = 0; i < kN; ++i) {
        const __m256 xi = _mm256_set1_ps(px[i]);
        const __m256 yi = _mm256_set1_ps(py[i]);
        const __m256 zi = _mm256_set1_ps(pz[i]);
        __m256 fxi = _mm256_setzero_ps();
        __m256 fyi = _mm256_setzero_ps();
        __m256 fzi = _mm256_setzero_ps();

        for (int j = 0; j < kN; j += 8) {
            __m256 xj = _mm256_loadu_ps(px + j);
            __m256 yj = _mm256_loadu_ps(py + j);
            __m256 zj = _mm256_loadu_ps(pz + j);
            __m256 dx  = _mm256_sub_ps(xj, xi);
            __m256 dy  = _mm256_sub_ps(yj, yi);
            __m256 dz  = _mm256_sub_ps(zj, zi);
            __m256 d2  = _mm256_fmadd_ps(dx, dx,
                         _mm256_fmadd_ps(dy, dy,
                         _mm256_fmadd_ps(dz, dz, veps)));
            __m256 inv = _mm256_div_ps(vgrav, d2);
            fxi = _mm256_fmadd_ps(inv, dx, fxi);
            fyi = _mm256_fmadd_ps(inv, dy, fyi);
            fzi = _mm256_fmadd_ps(inv, dz, fzi);
        }

        fx[i] = HorizSum8(fxi);
        fy[i] = HorizSum8(fyi);
        fz[i] = HorizSum8(fzi);
    }

    const __m256 vdt     = _mm256_set1_ps(kDt);
    const __m256 vdamp   = _mm256_set1_ps(kDamp);
    const __m256 vbnd    = _mm256_set1_ps(kBnd);
    const __m256 vnbnd   = _mm256_set1_ps(-kBnd);
    const __m256 negzero = _mm256_set1_ps(-0.0f);

    for (int i = 0; i < kN; i += 8) {
        __m256 vxi = _mm256_loadu_ps(vx + i);
        __m256 vyi = _mm256_loadu_ps(vy + i);
        __m256 vzi = _mm256_loadu_ps(vz + i);
        __m256 pxi = _mm256_loadu_ps(px + i);
        __m256 pyi = _mm256_loadu_ps(py + i);
        __m256 pzi = _mm256_loadu_ps(pz + i);

        vxi = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_loadu_ps(fx+i), vdt, vxi), vdamp);
        vyi = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_loadu_ps(fy+i), vdt, vyi), vdamp);
        vzi = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_loadu_ps(fz+i), vdt, vzi), vdamp);

        pxi = _mm256_fmadd_ps(vxi, vdt, pxi);
        pyi = _mm256_fmadd_ps(vyi, vdt, pyi);
        pzi = _mm256_fmadd_ps(vzi, vdt, pzi);

        // Reflect velocity and clamp position at world boundaries
        __m256 ohi_x = _mm256_cmp_ps(pxi, vbnd,  _CMP_GT_OS);
        __m256 olo_x = _mm256_cmp_ps(pxi, vnbnd, _CMP_LT_OS);
        vxi = _mm256_blendv_ps(vxi, _mm256_xor_ps(vxi, negzero), _mm256_or_ps(ohi_x, olo_x));
        pxi = _mm256_max_ps(vnbnd, _mm256_min_ps(vbnd, pxi));

        __m256 ohi_y = _mm256_cmp_ps(pyi, vbnd,  _CMP_GT_OS);
        __m256 olo_y = _mm256_cmp_ps(pyi, vnbnd, _CMP_LT_OS);
        vyi = _mm256_blendv_ps(vyi, _mm256_xor_ps(vyi, negzero), _mm256_or_ps(ohi_y, olo_y));
        pyi = _mm256_max_ps(vnbnd, _mm256_min_ps(vbnd, pyi));

        __m256 ohi_z = _mm256_cmp_ps(pzi, vbnd,  _CMP_GT_OS);
        __m256 olo_z = _mm256_cmp_ps(pzi, vnbnd, _CMP_LT_OS);
        vzi = _mm256_blendv_ps(vzi, _mm256_xor_ps(vzi, negzero), _mm256_or_ps(ohi_z, olo_z));
        pzi = _mm256_max_ps(vnbnd, _mm256_min_ps(vbnd, pzi));

        _mm256_storeu_ps(vx + i, vxi);
        _mm256_storeu_ps(vy + i, vyi);
        _mm256_storeu_ps(vz + i, vzi);
        _mm256_storeu_ps(px + i, pxi);
        _mm256_storeu_ps(py + i, pyi);
        _mm256_storeu_ps(pz + i, pzi);
    }
}

// ── RunCore ───────────────────────────────────────────────────

void GamePhysicsBenchmark::RunCore(UINT32 workerIndex, UINT32 /*totalWorkers*/) {
    if (workerIndex >= (UINT32)MAX_WORKERS) return;

    float* px = sPx[workerIndex];
    float* py = sPy[workerIndex];
    float* pz = sPz[workerIndex];
    float* vx = sVx[workerIndex];
    float* vy = sVy[workerIndex];
    float* vz = sVz[workerIndex];

    // Deterministic per-worker initial positions/velocities
    UINT64 seed = LCG_KNUTH_C ^ ((UINT64)(workerIndex + 1) * 7919ULL);
    for (int i = 0; i < N; ++i) {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        px[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 30));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        py[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 30));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        pz[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 30));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        vx[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 36));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        vy[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 36));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        vz[i] = (float)(INT32)(seed >> 33) * (1.0f / (float)(1U << 36));
    }

    const bool useAvx2 = CpuFeatures::Get().HasAVX2 && CpuFeatures::Get().HasXSave;
    SetIsa(useAvx2 ? "AVX2/FMA" : "scalar");
    if (useAvx2) CpuFeatures::EnableAvxState();

    if (useAvx2) {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_STEPS,
            [this, px, py, pz, vx, vy, vz](UINT64 n) {
                for (UINT64 s = 0; s < n; ++s)
                    StepAvx2(px, py, pz, vx, vy, vz);
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalPairs),
                                   n * PAIRS_PER_STEP, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    } else {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_STEPS,
            [this, px, py, pz, vx, vy, vz](UINT64 n) {
                for (UINT64 s = 0; s < n; ++s)
                    StepScalar(px, py, pz, vx, vy, vz);
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalPairs),
                                   n * PAIRS_PER_STEP, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    }

    volatile float sink = px[0] + py[0] + pz[0];
    (void)sink;
}
