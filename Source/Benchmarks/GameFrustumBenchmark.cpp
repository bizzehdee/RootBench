// Game Frustum Culling benchmark.
// For each frustum: test all 4096 AABBs using the "positive vertex" method
// (6 half-space tests per box). SoA layout lets SSE2 test 4 boxes per
// plane per iteration. Each worker runs the full N_FRUSTUMS × N_BOXES sweep.

#include "GameFrustumBenchmark.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"
#include <emmintrin.h>   // SSE2

// ── Static storage ────────────────────────────────────────────
float GameFrustumBenchmark::sMinX[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sMinY[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sMinZ[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sMaxX[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sMaxY[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sMaxZ[GameFrustumBenchmark::N_BOXES];
float GameFrustumBenchmark::sPlanes[GameFrustumBenchmark::N_FRUSTUMS][GameFrustumBenchmark::N_PLANES][4];

void GameFrustumBenchmark::Setup() {
    UINT64 seed = 0xFEEDFACECAFEBABEULL;
    auto nextF = [&](float lo, float hi) -> float {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        return lo + t * (hi - lo);
    };

    // Scene: random AABBs scattered in a [-200, 200] world
    for (int i = 0; i < N_BOXES; ++i) {
        float cx = nextF(-200.0f, 200.0f);
        float cy = nextF(-200.0f, 200.0f);
        float cz = nextF(-200.0f, 200.0f);
        float r  = nextF(0.5f, 5.0f);
        sMinX[i] = cx - r; sMaxX[i] = cx + r;
        sMinY[i] = cy - r; sMaxY[i] = cy + r;
        sMinZ[i] = cz - r; sMaxZ[i] = cz + r;
    }

    // 256 view frustums from cameras looking at origin from different positions
    // Build 256 frustums by distributing cameras on a 16×16 grid of angles.
    // Avoids sin/cos (no stdlib): uses normalised LCG direction vectors instead.
    for (int f = 0; f < N_FRUSTUMS; ++f) {
        // LCG-derived forward direction in the XZ plane
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float rx = (float)(INT32)(seed >> 32) * (1.0f / (float)(1U << 31));
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float rz = (float)(INT32)(seed >> 32) * (1.0f / (float)(1U << 31));

        // Normalise via fast reciprocal sqrt (Newton step)
        float mag2 = rx*rx + rz*rz + 1e-8f;
        float xhalf = 0.5f * mag2;
        int bits; __builtin_memcpy(&bits, &mag2, 4);
        bits = 0x5F375A86 - (bits >> 1);
        float inv; __builtin_memcpy(&inv, &bits, 4);
        inv  = inv * (1.5f - xhalf * inv * inv);
        float nx_f = rx * inv;
        float nz_f = rz * inv;

        float camX = nx_f * 100.0f;
        float camZ = nz_f * 100.0f;
        float near_d = 1.0f, far_d = 200.0f, fov = 0.5f;
        float sx = -nz_f, sz = nx_f;  // right-hand perpendicular in XZ plane

        // Near plane (inward normal: camera forward)
        sPlanes[f][0][0] =  nx_f; sPlanes[f][0][1] = 0.0f; sPlanes[f][0][2] =  nz_f;
        sPlanes[f][0][3] = -(camX*nx_f + camZ*nz_f) + near_d;
        // Far plane
        sPlanes[f][1][0] = -nx_f; sPlanes[f][1][1] = 0.0f; sPlanes[f][1][2] = -nz_f;
        sPlanes[f][1][3] =  (camX*nx_f + camZ*nz_f) + far_d;
        // Left side plane
        sPlanes[f][2][0] =  sx + nx_f*fov; sPlanes[f][2][1] = 0.0f; sPlanes[f][2][2] =  sz + nz_f*fov;
        sPlanes[f][2][3] = -(camX*sPlanes[f][2][0] + camZ*sPlanes[f][2][2]);
        // Right side plane
        sPlanes[f][3][0] = -sx + nx_f*fov; sPlanes[f][3][1] = 0.0f; sPlanes[f][3][2] = -sz + nz_f*fov;
        sPlanes[f][3][3] = -(camX*sPlanes[f][3][0] + camZ*sPlanes[f][3][2]);
        // Top plane
        sPlanes[f][4][0] = 0.0f; sPlanes[f][4][1] =  1.0f; sPlanes[f][4][2] = 0.0f;
        sPlanes[f][4][3] = -50.0f;
        // Bottom plane
        sPlanes[f][5][0] = 0.0f; sPlanes[f][5][1] = -1.0f; sPlanes[f][5][2] = 0.0f;
        sPlanes[f][5][3] = -50.0f;
    }
}

void GameFrustumBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    SetIsa("SSE2");

    TimeBox::RunWithProgress(GetBudgetUs(), 1,
        [this](UINT64) {
            __m128 acc  = _mm_setzero_ps();
            __m128 zero = _mm_setzero_ps();

            for (int f = 0; f < N_FRUSTUMS; ++f) {
                for (int p = 0; p < N_PLANES; ++p) {
                    __m128 vnx = _mm_set1_ps(sPlanes[f][p][0]);
                    __m128 vny = _mm_set1_ps(sPlanes[f][p][1]);
                    __m128 vnz = _mm_set1_ps(sPlanes[f][p][2]);
                    __m128 vd  = _mm_set1_ps(sPlanes[f][p][3]);

                    // SSE2: test 4 boxes per iteration using "positive vertex" method
                    for (int b = 0; b + 4 <= N_BOXES; b += 4) {
                        __m128 minx = _mm_loadu_ps(sMinX + b);
                        __m128 maxx = _mm_loadu_ps(sMaxX + b);
                        __m128 miny = _mm_loadu_ps(sMinY + b);
                        __m128 maxy = _mm_loadu_ps(sMaxY + b);
                        __m128 minz = _mm_loadu_ps(sMinZ + b);
                        __m128 maxz = _mm_loadu_ps(sMaxZ + b);

                        // Positive vertex for each axis: use max if normal positive, else min
                        __m128 cx   = _mm_cmpgt_ps(vnx, zero);
                        __m128 cy   = _mm_cmpgt_ps(vny, zero);
                        __m128 cz   = _mm_cmpgt_ps(vnz, zero);
                        __m128 pvx  = _mm_or_ps(_mm_and_ps(cx, maxx), _mm_andnot_ps(cx, minx));
                        __m128 pvy  = _mm_or_ps(_mm_and_ps(cy, maxy), _mm_andnot_ps(cy, miny));
                        __m128 pvz  = _mm_or_ps(_mm_and_ps(cz, maxz), _mm_andnot_ps(cz, minz));

                        // dot = nx*pvx + ny*pvy + nz*pvz + d  (< 0 means fully outside)
                        __m128 dot  = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vnx, pvx),
                                                             _mm_mul_ps(vny, pvy)),
                                                  _mm_add_ps(_mm_mul_ps(vnz, pvz), vd));
                        // Accumulate outside mask to prevent DCE
                        acc = _mm_or_ps(acc, _mm_cmplt_ps(dot, zero));
                    }
                }
            }

            // Prevent dead-code elimination of the whole loop
            volatile int sink = _mm_movemask_ps(acc);
            (void)sink;
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalTests),
                               TESTS_PER_SWEEP, __ATOMIC_RELAXED);
        },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
