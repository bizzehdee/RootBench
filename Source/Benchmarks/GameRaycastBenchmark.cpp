// Game Raycasting benchmark.
// Slab method: for each ray, test all 1024 AABBs using precomputed 1/dir to
// avoid per-test division. SSE2 inner loop tests 4 boxes at once per ray.
// Each worker independently sweeps the full N_RAYS × N_BOXES matrix.

#include "GameRaycastBenchmark.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"
#include <emmintrin.h>   // SSE2

// ── Static storage ────────────────────────────────────────────
float GameRaycastBenchmark::sRayOX[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sRayOY[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sRayOZ[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sRayIdx[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sRayIdy[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sRayIdz[GameRaycastBenchmark::N_RAYS];
float GameRaycastBenchmark::sMinX[GameRaycastBenchmark::N_BOXES];
float GameRaycastBenchmark::sMinY[GameRaycastBenchmark::N_BOXES];
float GameRaycastBenchmark::sMinZ[GameRaycastBenchmark::N_BOXES];
float GameRaycastBenchmark::sMaxX[GameRaycastBenchmark::N_BOXES];
float GameRaycastBenchmark::sMaxY[GameRaycastBenchmark::N_BOXES];
float GameRaycastBenchmark::sMaxZ[GameRaycastBenchmark::N_BOXES];

void GameRaycastBenchmark::Setup() {
    UINT64 seed = 0x9E3779B97F4A7C15ULL;
    auto nextF = [&](float lo, float hi) -> float {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        return lo + t * (hi - lo);
    };

    // Scene: AABBs scattered in a [-50, 50] world
    for (int i = 0; i < N_BOXES; ++i) {
        float cx = nextF(-50.0f, 50.0f);
        float cy = nextF(-10.0f, 10.0f);
        float cz = nextF(-50.0f, 50.0f);
        float r  = nextF(0.25f, 3.0f);
        sMinX[i] = cx - r; sMaxX[i] = cx + r;
        sMinY[i] = cy - r; sMaxY[i] = cy + r;
        sMinZ[i] = cz - r; sMaxZ[i] = cz + r;
    }

    // Rays: cast from positions around the scene toward the centre
    for (int i = 0; i < N_RAYS; ++i) {
        sRayOX[i] = nextF(-80.0f, 80.0f);
        sRayOY[i] = nextF(-20.0f, 20.0f);
        sRayOZ[i] = nextF(-80.0f, 80.0f);
        // Direction: aim roughly toward origin with spread
        float dx = nextF(-10.0f, 10.0f) - sRayOX[i] * 0.1f;
        float dy = nextF(-5.0f,  5.0f)  - sRayOY[i] * 0.1f;
        float dz = nextF(-10.0f, 10.0f) - sRayOZ[i] * 0.1f;
        // Normalise by magnitude (approx — avoid stdlib sqrt)
        float mag2 = dx*dx + dy*dy + dz*dz;
        // Use reciprocal sqrt approximation (Newton step from rsqrtss)
        float xhalf = 0.5f * mag2;
        int   bits;
        __builtin_memcpy(&bits, &mag2, 4);
        bits = 0x5F375A86 - (bits >> 1);
        float inv;
        __builtin_memcpy(&inv, &bits, 4);
        inv = inv * (1.5f - xhalf * inv * inv);  // one Newton-Raphson step
        dx *= inv; dy *= inv; dz *= inv;
        // Avoid zero direction components (add tiny offset)
        if (dx == 0.0f) dx = 1e-9f;
        if (dy == 0.0f) dy = 1e-9f;
        if (dz == 0.0f) dz = 1e-9f;
        sRayIdx[i] = 1.0f / dx;
        sRayIdy[i] = 1.0f / dy;
        sRayIdz[i] = 1.0f / dz;
    }
}

void GameRaycastBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    SetIsa("SSE2");

    TimeBox::RunWithProgress(GetBudgetUs(), 1,
        [this](UINT64) {
            UINT64 hits = 0;

            for (int r = 0; r < N_RAYS; ++r) {
                __m128 vox  = _mm_set1_ps(sRayOX[r]);
                __m128 voy  = _mm_set1_ps(sRayOY[r]);
                __m128 voz  = _mm_set1_ps(sRayOZ[r]);
                __m128 vidx = _mm_set1_ps(sRayIdx[r]);
                __m128 vidy = _mm_set1_ps(sRayIdy[r]);
                __m128 vidz = _mm_set1_ps(sRayIdz[r]);
                __m128 zero = _mm_setzero_ps();

                for (int b = 0; b + 4 <= N_BOXES; b += 4) {
                    __m128 minx = _mm_loadu_ps(sMinX + b);
                    __m128 maxx = _mm_loadu_ps(sMaxX + b);
                    __m128 miny = _mm_loadu_ps(sMinY + b);
                    __m128 maxy = _mm_loadu_ps(sMaxY + b);
                    __m128 minz = _mm_loadu_ps(sMinZ + b);
                    __m128 maxz = _mm_loadu_ps(sMaxZ + b);

                    // Slab X
                    __m128 tx1 = _mm_mul_ps(_mm_sub_ps(minx, vox), vidx);
                    __m128 tx2 = _mm_mul_ps(_mm_sub_ps(maxx, vox), vidx);
                    __m128 txn = _mm_min_ps(tx1, tx2);
                    __m128 txx = _mm_max_ps(tx1, tx2);

                    // Slab Y
                    __m128 ty1 = _mm_mul_ps(_mm_sub_ps(miny, voy), vidy);
                    __m128 ty2 = _mm_mul_ps(_mm_sub_ps(maxy, voy), vidy);
                    __m128 tyn = _mm_min_ps(ty1, ty2);
                    __m128 tyx = _mm_max_ps(ty1, ty2);

                    // Slab Z
                    __m128 tz1 = _mm_mul_ps(_mm_sub_ps(minz, voz), vidz);
                    __m128 tz2 = _mm_mul_ps(_mm_sub_ps(maxz, voz), vidz);
                    __m128 tzn = _mm_min_ps(tz1, tz2);
                    __m128 tzx = _mm_max_ps(tz1, tz2);

                    // tmin = max(txn, tyn, tzn), tmax = min(txx, tyx, tzx)
                    __m128 tmin = _mm_max_ps(txn, _mm_max_ps(tyn, tzn));
                    __m128 tmax = _mm_min_ps(txx, _mm_min_ps(tyx, tzx));

                    // hit if tmax >= tmin && tmax >= 0
                    __m128 hit = _mm_and_ps(_mm_cmpge_ps(tmax, tmin),
                                            _mm_cmpge_ps(tmax, zero));

                    // Count hits (movemask gives 4-bit result)
                    hits += (UINT64)__builtin_popcount((UINT32)_mm_movemask_ps(hit));
                }
            }

            volatile UINT64 sink = hits;
            (void)sink;
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalTests),
                               TESTS_PER_SWEEP, __ATOMIC_RELAXED);
        },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
