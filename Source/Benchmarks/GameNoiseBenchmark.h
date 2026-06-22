#pragma once
// Game Procedural Noise benchmark.
// Each worker evaluates 3D value noise over a 128×128 grid with 8 octaves
// (fbm). Represents terrain generation, texture synthesis, and procedural
// content pipelines. Score: GAME pts (1000 = AMD Ryzen 9 5950X).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"
#include "GameScore.h"

class GameNoiseBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Game Procedural Noise"; }
    const char* GetDescription() const override {
        return "3D value noise, 128x128 grid, 8 octaves (fbm); each core full grid";
    }
    const char* GetCategory() const override { return "Gaming"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return GAME_WEIGHT_NOISE; }

    UINT64      GetBudgetUs()  const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore()     const override {
        return mTotalEvals > 0
               ? (mTotalEvals / ScoreDurationUs()) * 1000 / GAME_REF_NOISE_EVALS
               : 0;
    }
    const char* GetUnit()      const override { return "GAME pts"; }
    UINT64      GetRawMetric() const override { return mTotalEvals / ScoreDurationUs(); }
    const char* GetRawUnit()   const override { return "Mevals/us"; }

    void Setup()  override;
    void PreRun() override { mTotalEvals = 0; }
    void Run()    override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int GRID   = 128;
    static constexpr int OCTAVES = 8;

private:
    static constexpr UINT64 EVALS_PER_SWEEP = (UINT64)GRID * GRID;

    // 256-element permutation table (doubled to 512 to handle wrap-around)
    static UINT8  sPerm[512];
    // 256-element gradient values ([-1, 1])
    static float  sGrad[256];

    volatile UINT64 mTotalEvals = 0;
};
