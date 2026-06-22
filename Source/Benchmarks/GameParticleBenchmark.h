#pragma once
// Game Particle System benchmark.
// 131072 particles in SoA layout. Workers stripe the array. Each tick:
// pos += vel*dt, vel *= drag, life -= decay. Represents VFX emitters,
// fire, smoke, and weather effects. Score: GAME pts (1000 = AMD Ryzen 9 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GameParticleBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Particle System"; }
    const char* GetDescription() const override {
        return "131072-particle SoA tick: pos+vel integration, lifetime decay";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::MultiOnly; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_PARTICLE; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalUpdates > 0
               ? (mTotalUpdates / ScoreDurationUs()) * 1000 / GAME_REF_PARTICLE_UPD
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalUpdates / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Mpart-upd/us"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalUpdates = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr UINT32 TOTAL_N = 131072U;

private:
    static constexpr UINT64 CHUNK_PASSES = 32ULL;

    float* mPosX = nullptr;
    float* mPosY = nullptr;
    float* mPosZ = nullptr;
    float* mVelX = nullptr;
    float* mVelY = nullptr;
    float* mVelZ = nullptr;
    float* mLife = nullptr;

    volatile UINT64 mTotalUpdates = 0;
};
