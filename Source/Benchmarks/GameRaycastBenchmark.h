#pragma once
// Game Raycasting benchmark.
// Tests 1024 rays against 1024 AABBs using the slab method. Each worker runs
// the full 1024×1024 sweep independently. Represents AI line-of-sight checks,
// bullet traces, and physics queries. Score: GAME pts (1000 = AMD Ryzen 9 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GameRaycastBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Raycasting (AABB)"; }
    const char* GetDescription() const override {
        return "1024 rays vs 1024 AABBs (slab method); each core runs full sweep";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_RAYCAST; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalTests > 0
               ? (mTotalTests / ScoreDurationUs()) * 1000 / GAME_REF_RAYCAST_TESTS
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalTests / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Mrays/us"; }

    void Setup()  override;
    void PreRun() override { mTotalTests = 0; }
    void Run()    override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int N_RAYS  = 1024;
    static constexpr int N_BOXES = 1024;

private:
    static constexpr UINT64 TESTS_PER_SWEEP = (UINT64)N_RAYS * N_BOXES;

    // Ray data: origin + precomputed inverse direction
    static float sRayOX[N_RAYS],  sRayOY[N_RAYS],  sRayOZ[N_RAYS];
    static float sRayIdx[N_RAYS], sRayIdy[N_RAYS], sRayIdz[N_RAYS]; // 1/dir

    // AABB data: SoA min/max
    static float sMinX[N_BOXES], sMinY[N_BOXES], sMinZ[N_BOXES];
    static float sMaxX[N_BOXES], sMaxY[N_BOXES], sMaxZ[N_BOXES];

    volatile UINT64 mTotalTests = 0;
};
