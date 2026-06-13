#pragma once
// AI Suitability tier evaluation based on detected CPU features.

#include "CpuFeatures.h"

namespace AiSuitability {

enum class Tier : UINT32 {
    Limited   = 0,  // SSE4.2 / scalar only
    Good      = 1,  // AVX2 + FMA  (e.g. Ryzen 5800X baseline)
    VeryGood  = 2,  // AVX2 + FMA + AVX-VNNI
    Excellent = 3,  // AVX-512F + AVX-512VNNI
};

struct FeatureRow {
    const char* name;
    const char* description;
    bool        supported;
    bool        tierCritical;
};

inline Tier Evaluate(const CpuFeatures::Features& f) {
    if (f.HasAVX512F && f.HasAVX512VNNI)
        return Tier::Excellent;
    if (f.HasAVX2 && f.HasFMA && f.HasAVXVNNI)
        return Tier::VeryGood;
    if (f.HasAVX2 && f.HasFMA)
        return Tier::Good;
    return Tier::Limited;
}

inline const char* TierName(Tier t) {
    switch (t) {
        case Tier::Excellent: return "Excellent";
        case Tier::VeryGood:  return "Very Good";
        case Tier::Good:      return "Good";
        default:              return "Limited";
    }
}

// Short assessment paragraph per tier (single string, line-wrap at caller).
inline const char* TierSummary(Tier t) {
    switch (t) {
        case Tier::Excellent:
            return "AVX-512 with VNNI: native INT8/INT4 vector neural network instructions "
                   "run quantized LLM inference at peak efficiency. This is the highest "
                   "tier currently measurable on x86 without discrete GPU.";
        case Tier::VeryGood:
            return "AVX-VNNI extends AVX2 with INT8 dot-product instructions, closing most "
                   "of the gap to AVX-512 VNNI for quantized LLM workloads. FMA enables "
                   "efficient FP32 attention computation.";
        case Tier::Good:
            return "AVX2 + FMA supports efficient INT8 inference via MADDUBS/MADD and "
                   "FP32 attention via vectorised FMA. This is the Ryzen 7 5800X baseline "
                   "against which AI scores are calibrated (1000 AI pts).";
        default:
            return "No AVX2 detected. INT8 inference falls back to scalar loops; "
                   "throughput is significantly reduced. Consider upgrading to a "
                   "Zen2+ or Tiger Lake+ processor for practical LLM performance.";
    }
}

} // namespace AiSuitability
