#pragma once
// Integer serial-latency benchmark: one dependency chain, single-core.
// Score: MOPS (single-core throughput of a serial dependency chain).

#include "LongBenchmarkBase.h"
#include "RunConfig.h"

class IntLatencyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Int Latency (Serial)"; }
    const char* GetDescription() const override {
        return "Single add+mul+xor dependency chain; measures per-op latency (120s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::SingleOnly; }

    UINT64 GetBudgetUs() const override { return RunConfig::GetTestBudgetUs(); }
    UINT64      GetScore() const override { return mTotalIter / ScoreDurationUs(); }
    const char* GetUnit()  const override { return "MOPS"; }

    void PreRun() override { mTotalIter = 0; }
    void Run() override;

private:
    static constexpr UINT64 CHUNK_SIZE = 1000000ULL;
    UINT64 mTotalIter = 0;
};
