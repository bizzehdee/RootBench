#pragma once
// Branch predictor stress benchmark: unpredictable data-dependent branches.
// Working set fits in L1/L2 to measure branch predictor, not memory.
// Multi-core, time-boxed. Score: M-branch/s.

#include "LongBenchmarkBase.h"
#include "RunConfig.h"

class BranchBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Branch Prediction"; }
    const char* GetDescription() const override {
        return "Data-dependent branches on random bytes; stresses branch predictor (150s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore() const override { return mTotalIter / ScoreDurationUs(); }
    const char* GetUnit()  const override { return "Mbranch/s"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalIter = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINTN  BUF_BYTES  = 64 * BYTES_PER_KB;  // 64 KB — fits in L2
    static constexpr UINT64 CHUNK_SIZE = 1000000ULL;

    UINT8*  mData     = nullptr;
    volatile UINT64 mTotalIter = 0;
};
