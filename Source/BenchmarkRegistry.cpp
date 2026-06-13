// Benchmark registry: fixed-capacity array of benchmark pointers.

#include "BenchmarkRegistry.h"
#include "Freestanding.h"

IBenchmark* BenchmarkRegistry::sBenchmarks[MAX_BENCHMARKS] = {};
UINTN       BenchmarkRegistry::sCount = 0;

void BenchmarkRegistry::Register(IBenchmark* benchmark) {
    if (benchmark && sCount < MAX_BENCHMARKS) {
        sBenchmarks[sCount++] = benchmark;
    }
}

IBenchmark** BenchmarkRegistry::GetAll() {
    return sBenchmarks;
}

UINTN BenchmarkRegistry::Count() {
    return sCount;
}

void BenchmarkRegistry::Clear() {
    sCount = 0;
}

// ── Category helpers ──────────────────────────────────────────

// Populate cats[] with unique category strings from the registry.
// Returns count. With ≤32 benchmarks this is trivially fast.
static UINT32 BuildCategoryList(const char** cats, UINT32 maxCats) {
    UINT32 count = 0;
    for (UINTN i = 0; i < BenchmarkRegistry::Count(); ++i) {
        const char* cat = BenchmarkRegistry::GetAll()[i]->GetCategory();
        bool found = false;
        for (UINT32 j = 0; j < count; ++j)
            if (StrCmp(cats[j], cat) == 0) { found = true; break; }
        if (!found && count < maxCats) cats[count++] = cat;
    }
    return count;
}

UINT32 BenchmarkRegistry::GetCategoryCount() {
    const char* cats[16];
    return BuildCategoryList(cats, 16);
}

const char* BenchmarkRegistry::GetCategoryName(UINT32 index) {
    const char* cats[16];
    UINT32 count = BuildCategoryList(cats, 16);
    return (index < count) ? cats[index] : "";
}

UINT32 BenchmarkRegistry::GetBenchmarksInCategory(const char* category,
                                                    IBenchmark** out, UINT32 maxCount) {
    UINT32 found = 0;
    for (UINTN i = 0; i < sCount && found < maxCount; ++i) {
        if (StrCmp(sBenchmarks[i]->GetCategory(), category) == 0)
            out[found++] = sBenchmarks[i];
    }
    return found;
}
