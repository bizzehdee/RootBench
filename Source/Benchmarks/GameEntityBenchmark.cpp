// Game Entity Update benchmark.
// Simulates an ECS movement system: each worker ticks its stripe of 65536
// entities (pos += vel*dt, vel *= damping, health += regen). SoA layout
// allows the compiler to auto-vectorise the tight inner loops with AVX2.

#include "GameEntityBenchmark.h"
#include "Freestanding.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"

static constexpr float kDt         = 0.016f;     // ~60Hz frame time
static constexpr float kDamping    = 0.98f;
static constexpr float kWorldSize  = 500.0f;
static constexpr float kMaxHealth  = 100.0f;
static constexpr float kRegenRate  = 0.5f;

void GameEntityBenchmark::Setup() {
    mPosX   = new float[TOTAL_N];
    mPosY   = new float[TOTAL_N];
    mPosZ   = new float[TOTAL_N];
    mVelX   = new float[TOTAL_N];
    mVelY   = new float[TOTAL_N];
    mVelZ   = new float[TOTAL_N];
    mHealth = new float[TOTAL_N];

    if (!mPosX || !mPosY || !mPosZ || !mVelX || !mVelY || !mVelZ || !mHealth) {
        SetNote("Out of memory");
        return;
    }

    UINT64 seed = 0xC0FFEE12DEADBEEFULL;
    for (UINT32 i = 0; i < TOTAL_N; ++i) {
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        float t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosX[i] = (t * 2.0f - 1.0f) * kWorldSize;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosY[i] = (t * 2.0f - 1.0f) * kWorldSize;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mPosZ[i] = (t * 2.0f - 1.0f) * kWorldSize;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelX[i] = (t * 2.0f - 1.0f) * 5.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelY[i] = (t * 2.0f - 1.0f) * 5.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mVelZ[i] = (t * 2.0f - 1.0f) * 5.0f;
        seed = seed * LCG_KNUTH_A + LCG_KNUTH_C;
        t = (float)((UINT32)(seed >> 32) >> 8) * (1.0f / 16777216.0f);
        mHealth[i] = t * kMaxHealth;
    }
}

void GameEntityBenchmark::Teardown() {
    delete[] mPosX;   mPosX   = nullptr;
    delete[] mPosY;   mPosY   = nullptr;
    delete[] mPosZ;   mPosZ   = nullptr;
    delete[] mVelX;   mVelX   = nullptr;
    delete[] mVelY;   mVelY   = nullptr;
    delete[] mVelZ;   mVelZ   = nullptr;
    delete[] mHealth; mHealth = nullptr;
}

void GameEntityBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    if (!mPosX) { SetNote("Out of memory"); return; }
    if (totalWorkers == 0 || workerIndex >= totalWorkers) return;

    UINT32 chunk = TOTAL_N / totalWorkers;
    UINT32 start = workerIndex * chunk;
    UINT32 end   = (workerIndex + 1 == totalWorkers) ? TOTAL_N : start + chunk;
    UINT32 count = end - start;

    float* px = mPosX   + start;
    float* py = mPosY   + start;
    float* pz = mPosZ   + start;
    float* vx = mVelX   + start;
    float* vy = mVelY   + start;
    float* vz = mVelZ   + start;
    float* hp = mHealth + start;

    SetIsa("SSE2");  // compiler auto-vectorises with SSE2/AVX2 from -O2

    TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_PASSES,
        [this, px, py, pz, vx, vy, vz, hp, count](UINT64 n) {
            for (UINT64 pass = 0; pass < n; ++pass) {
                for (UINT32 i = 0; i < count; ++i) {
                    // Integrate position
                    px[i] += vx[i] * kDt;
                    py[i] += vy[i] * kDt;
                    pz[i] += vz[i] * kDt;
                    // Apply damping
                    vx[i] *= kDamping;
                    vy[i] *= kDamping;
                    vz[i] *= kDamping;
                    // Health regen (exponential approach to max)
                    hp[i] += (kMaxHealth - hp[i]) * kRegenRate * kDt;
                }
            }
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalUpdates),
                               n * (UINT64)count, __ATOMIC_RELAXED);
        },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });

    volatile float sink = px[0] + hp[0];
    (void)sink;
}
