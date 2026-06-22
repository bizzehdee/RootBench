// Game Procedural Noise benchmark.
// Implements 3D value noise via trilinear interpolation and a permutation
// table. Each full-grid sweep evaluates GRID*GRID*OCTAVES samples. The hash
// lookups (random table accesses) stress the L1/L2 cache and integer pipeline.

#include "GameNoiseBenchmark.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"

// ── Static storage ────────────────────────────────────────────
UINT8 GameNoiseBenchmark::sPerm[512];
float GameNoiseBenchmark::sGrad[256];

// ── Integer fast-floor ────────────────────────────────────────
static inline int IFloor(float x) {
    int i = (int)x;
    return i - (x < (float)i ? 1 : 0);
}

// ── Smooth step (quintic fade) ────────────────────────────────
static inline float Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ── Linear interpolation ──────────────────────────────────────
static inline float Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// ── 3D value noise ────────────────────────────────────────────
static float Noise3D(float x, float y, float z,
                     const UINT8* perm, const float* grad)
{
    int xi = IFloor(x) & 255;
    int yi = IFloor(y) & 255;
    int zi = IFloor(z) & 255;
    float fx = x - (float)IFloor(x);
    float fy = y - (float)IFloor(y);
    float fz = z - (float)IFloor(z);
    float u = Fade(fx), v = Fade(fy), w = Fade(fz);

    // Hash all 8 corners of the unit cube
    int p00 = perm[perm[xi    ] + yi    ];
    int p10 = perm[perm[xi + 1] + yi    ];
    int p01 = perm[perm[xi    ] + yi + 1];
    int p11 = perm[perm[xi + 1] + yi + 1];

    float g000 = grad[perm[p00 + zi    ]];
    float g100 = grad[perm[p10 + zi    ]];
    float g010 = grad[perm[p01 + zi    ]];
    float g110 = grad[perm[p11 + zi    ]];
    float g001 = grad[perm[p00 + zi + 1]];
    float g101 = grad[perm[p10 + zi + 1]];
    float g011 = grad[perm[p01 + zi + 1]];
    float g111 = grad[perm[p11 + zi + 1]];

    float x1 = Lerp(g000, g100, u);
    float x2 = Lerp(g010, g110, u);
    float x3 = Lerp(g001, g101, u);
    float x4 = Lerp(g011, g111, u);
    return Lerp(Lerp(x1, x2, v), Lerp(x3, x4, v), w);
}

void GameNoiseBenchmark::Setup() {
    UINT64 seed = 0xA17F4B3C927E5D01ULL;

    auto nextU8 = [&]() -> UINT8 {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        return (UINT8)(seed >> 56);
    };

    // Fisher-Yates shuffle for permutation table
    for (int i = 0; i < 256; ++i) sPerm[i] = (UINT8)i;
    for (int i = 255; i > 0; --i) {
        int j = (int)(nextU8()) % (i + 1);
        UINT8 tmp = sPerm[i]; sPerm[i] = sPerm[j]; sPerm[j] = tmp;
    }
    for (int i = 0; i < 256; ++i) sPerm[i + 256] = sPerm[i];

    // Random gradient values in [-1, 1]
    for (int i = 0; i < 256; ++i) {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        sGrad[i] = (float)(INT32)(seed >> 32) * (1.0f / (float)(1U << 31));
    }
}

void GameNoiseBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    SetIsa("scalar");

    TimeBox::RunWithProgress(GetBudgetUs(), 1,
        [this](UINT64) {
            float acc = 0.0f;
            // Evaluate fbm over GRID×GRID grid points
            for (int gy = 0; gy < GRID; ++gy) {
                for (int gx = 0; gx < GRID; ++gx) {
                    float sx = (float)gx * (1.0f / (float)GRID);
                    float sy = (float)gy * (1.0f / (float)GRID);
                    float amplitude = 1.0f, frequency = 1.0f, value = 0.0f;
                    for (int oct = 0; oct < OCTAVES; ++oct) {
                        value     += amplitude * Noise3D(sx * frequency,
                                                         sy * frequency,
                                                         (float)oct * 0.317f,
                                                         sPerm, sGrad);
                        amplitude *= 0.5f;
                        frequency *= 2.0f;
                    }
                    acc += value;
                }
            }
            volatile float sink = acc;
            (void)sink;
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalEvals),
                               EVALS_PER_SWEEP, __ATOMIC_RELAXED);
        },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
