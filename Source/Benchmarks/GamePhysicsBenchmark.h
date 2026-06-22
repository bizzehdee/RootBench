#pragma once
// Game Physics (N-Body) benchmark.
// Each worker independently simulates N=128 point masses with pairwise
// inverse-square gravity (no sqrt; L1-resident SoA). Stresses FMA throughput
// and ILP. AVX2/FMA path processes 8 j-particles per inner iteration.
// Score: normalised GAME pts (1000 = AMD Ryzen 9 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GamePhysicsBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Physics (N-Body)"; }
    const char* GetDescription() const override {
        return "Pairwise gravity on N=128 particles/core; AVX2/FMA or scalar";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_PHYSICS; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalPairs > 0
               ? (mTotalPairs / ScoreDurationUs()) * 1000 / GAME_REF_PHYSICS_PAIRS
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalPairs / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Mpairs/us"; }

    void PreRun() override { mTotalPairs = 0; }
    void Run()    override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int N           = 128;
    static constexpr int MAX_WORKERS = 32;

private:
    static constexpr UINT64 CHUNK_STEPS    = 8ULL;
    static constexpr UINT64 PAIRS_PER_STEP = (UINT64)N * N;

    static float sPx[MAX_WORKERS][N];
    static float sPy[MAX_WORKERS][N];
    static float sPz[MAX_WORKERS][N];
    static float sVx[MAX_WORKERS][N];
    static float sVy[MAX_WORKERS][N];
    static float sVz[MAX_WORKERS][N];

    volatile UINT64 mTotalPairs = 0;
};
