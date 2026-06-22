#pragma once
// Game Frustum Culling benchmark.
// Tests 4096 AABBs against 256 view frustums (6 planes each). Each worker
// independently runs the full test suite. Represents GPU draw-call culling
// and occlusion queries on every render frame. Score: GAME pts (1000 = 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GameFrustumBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Frustum Culling"; }
    const char* GetDescription() const override {
        return "4096 AABBs vs 256 frustums (6-plane); each core runs full suite";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_FRUSTUM; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalTests > 0
               ? (mTotalTests / ScoreDurationUs()) * 1000 / GAME_REF_FRUSTUM_TESTS
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalTests / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Mtests/us"; }

    void Setup()    override;
    void PreRun()   override { mTotalTests = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int N_BOXES    = 4096;
    static constexpr int N_FRUSTUMS = 256;
    static constexpr int N_PLANES   = 6;

private:
    static constexpr UINT64 TESTS_PER_SWEEP = (UINT64)N_BOXES * N_FRUSTUMS;

    // SoA AABB storage (float per component, 4096 entries each)
    static float sMinX[N_BOXES];
    static float sMinY[N_BOXES];
    static float sMinZ[N_BOXES];
    static float sMaxX[N_BOXES];
    static float sMaxY[N_BOXES];
    static float sMaxZ[N_BOXES];

    // Frustum planes: [frustum][plane][0..3] = nx, ny, nz, d
    static float sPlanes[N_FRUSTUMS][N_PLANES][4];

    volatile UINT64 mTotalTests = 0;
};
