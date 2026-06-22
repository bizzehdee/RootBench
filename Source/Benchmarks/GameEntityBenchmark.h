#pragma once
// Game Entity Update (ECS) benchmark.
// 65536 entities in SoA layout (posX/Y/Z, velX/Y/Z, health). Workers stripe
// the array. Each tick: pos += vel*dt, vel *= damping, health regens toward max.
// Represents a game main-loop entity update pass. Score: GAME pts (1000 = 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GameEntityBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Entity Update (ECS)"; }
    const char* GetDescription() const override {
        return "65536-entity SoA tick: pos+vel integration, health regen (multi-core)";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::MultiOnly; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_ENTITY; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalUpdates > 0
               ? (mTotalUpdates / ScoreDurationUs()) * 1000 / GAME_REF_ENTITY_UPD
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalUpdates / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Ment-upd/us"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalUpdates = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr UINT32 TOTAL_N = 65536U;

private:
    static constexpr UINT64 CHUNK_PASSES = 16ULL;

    float* mPosX   = nullptr;
    float* mPosY   = nullptr;
    float* mPosZ   = nullptr;
    float* mVelX   = nullptr;
    float* mVelY   = nullptr;
    float* mVelZ   = nullptr;
    float* mHealth = nullptr;

    volatile UINT64 mTotalUpdates = 0;
};
