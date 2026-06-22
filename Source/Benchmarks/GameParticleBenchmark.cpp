// Game Particle System benchmark.
// Pure SoA layout enables aggressive auto-vectorisation. The inner loop is
// entirely branchless: position/velocity integration and life decay. No
// respawn path keeps the SIMD pipeline saturated.

#include "GameParticleBenchmark.h"
#include "Freestanding.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"

static constexpr float kDt   = 0.016f;
static constexpr float kDrag = 0.995f;
static constexpr float kDecay = 0.001f;

void GameParticleBenchmark::Setup() {
    mPosX = new float[TOTAL_N];
    mPosY = new float[TOTAL_N];
    mPosZ = new float[TOTAL_N];
    mVelX = new float[TOTAL_N];
    mVelY = new float[TOTAL_N];
    mVelZ = new float[TOTAL_N];
    mLife = new float[TOTAL_N];

    if (!mPosX || !mPosY || !mPosZ || !mVelX || !mVelY || !mVelZ || !mLife) {
        SetNote("Out of memory");
        return;
    }

    UINT64 seed = 0x1234567890ABCDEFULL;
    for (UINT32 i = 0; i < TOTAL_N; ++i) {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosX[i] = (t * 2.0f - 1.0f) * 100.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosY[i] = t * 50.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosZ[i] = (t * 2.0f - 1.0f) * 100.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelX[i] = (t * 2.0f - 1.0f) * 2.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelY[i] = t * 10.0f + 2.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelZ[i] = (t * 2.0f - 1.0f) * 2.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mLife[i]  = t;
    }
}

void GameParticleBenchmark::Teardown() {
    delete[] mPosX; mPosX = nullptr;
    delete[] mPosY; mPosY = nullptr;
    delete[] mPosZ; mPosZ = nullptr;
    delete[] mVelX; mVelX = nullptr;
    delete[] mVelY; mVelY = nullptr;
    delete[] mVelZ; mVelZ = nullptr;
    delete[] mLife; mLife = nullptr;
}

void GameParticleBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    if (!mPosX) { SetNote("Out of memory"); return; }
    if (totalWorkers == 0 || workerIndex >= totalWorkers) return;

    UINT32 chunk = TOTAL_N / totalWorkers;
    UINT32 start = workerIndex * chunk;
    UINT32 end   = (workerIndex + 1 == totalWorkers) ? TOTAL_N : start + chunk;
    UINT32 count = end - start;

    float* px = mPosX + start;
    float* py = mPosY + start;
    float* pz = mPosZ + start;
    float* vx = mVelX + start;
    float* vy = mVelY + start;
    float* vz = mVelZ + start;
    float* lf = mLife + start;

    SetIsa("SSE2");  // tight branchless loop: compiler emits AVX2 with -O2

    TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_PASSES,
        [this, px, py, pz, vx, vy, vz, lf, count](UINT64 n) {
            for (UINT64 pass = 0; pass < n; ++pass) {
                for (UINT32 i = 0; i < count; ++i) {
                    px[i] += vx[i] * kDt;
                    py[i] += vy[i] * kDt;
                    pz[i] += vz[i] * kDt;
                    vx[i] *= kDrag;
                    vy[i] *= kDrag;
                    vz[i] *= kDrag;
                    lf[i] -= kDecay;
                }
            }
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalUpdates),
                               n * (UINT64)count, __ATOMIC_RELAXED);
        },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });

    volatile float sink = px[0] + lf[0];
    (void)sink;
}
