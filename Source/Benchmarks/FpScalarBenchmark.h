#pragma once
// Scalar FP + divide benchmark: mixed add/mul/div on doubles.
// Multi-core, time-boxed. Score: MFLOP/s.

#include "LongBenchmarkBase.h"
#include "RunConfig.h"

class FpScalarBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "FP Scalar + Divide"; }
    const char* GetDescription() const override {
        return "Mixed scalar add/mul/div on doubles; stresses FP divider (180s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return RunConfig::GetTestBudgetUs(); }
    // 4 flops per inner iteration (add, mul, div, add)
    UINT64      GetScore() const override { return (mTotalIter * 4ULL) / GetBudgetUs(); }
    const char* GetUnit()  const override { return "MFLOP/s"; }

    void PreRun()  override { mTotalIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 CHUNK_SIZE = 500000ULL;
    volatile UINT64 mTotalIter = 0;
};
