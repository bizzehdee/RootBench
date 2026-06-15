#pragma once
// CPU power virus: AVX2+FMA max-power kernel for 30 minutes.
// Drives all selected cores at peak FP draw to stress Vcore, power delivery,
// and cooling. Score = sustained GFLOP/s; throttling shows as a lower score.

#include "LongBenchmarkBase.h"
#include "RunConfig.h"

class StressCpuPowerBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Stress: CPU Power Virus"; }
    const char* GetDescription() const override {
        return "AVX2+FMA peak-power heat soak; score = sustained GFLOP/s";
    }
    const char* GetCategory()    const override { return "Stress"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::MultiOnly; }
    bool          IncludeInCategoryScore() const override { return false; }

    UINT64 GetBudgetUs() const override { return RunConfig::GetStressBudgetUs(); }
    // 10 accumulators × 4 doubles × 2 flops/FMA = 80 flops per iteration
    UINT64      GetScore() const override { return (mTotalIter * 80ULL) / ScoreDurationUs() / 1000ULL; }
    const char* GetUnit()  const override { return "GFLOP/s"; }

    void PreRun()  override { mTotalIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 CHUNK_SIZE = 100000ULL;

    volatile UINT64 mTotalIter = 0;
};
