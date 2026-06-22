#pragma once
// Gaming benchmark constants: reference values and weights.
// Reference system (baseline = 1000 GAME pts per sub-test):
//   AMD Ryzen 9 5950X (16-core/32-thread, ~4.7 GHz all-core),
//   32 GB DDR4-3766 CL16 dual-channel.
//
// *** UNCALIBRATED: these reference values are estimates. To recalibrate,
// run "Run All Gaming", read GetRawMetric() on each benchmark on the
// reference hardware, set each GAME_REF_* to that value, and bump
// GAME_SCORE_VERSION. ***

#include "UefiTypes.h"

// Schema version — bump when reference values change.
static constexpr UINT32 GAME_SCORE_VERSION = 1;

// ── Reference values (raw metric / µs on the 5950X baseline) ──
// Physics: pairwise interactions/µs (N=128 particles, O(N²), multi-core AVX2)
static constexpr UINT64 GAME_REF_PHYSICS_PAIRS  = 30000000ULL;  // UNCALIBRATED
// Entity: entity-updates/µs (65536 entities SoA, multi-core)
static constexpr UINT64 GAME_REF_ENTITY_UPD     = 150000ULL;    // UNCALIBRATED
// Frustum: AABB-frustum tests/µs (4096 boxes × 256 frustums, multi-core)
static constexpr UINT64 GAME_REF_FRUSTUM_TESTS  = 30000ULL;     // UNCALIBRATED
// Noise: grid-point evaluations/µs (128×128 grid × 8 octaves, multi-core)
static constexpr UINT64 GAME_REF_NOISE_EVALS    = 500ULL;       // UNCALIBRATED
// Particle: particle-updates/µs (131072 particles SoA, multi-core)
static constexpr UINT64 GAME_REF_PARTICLE_UPD   = 15000ULL;     // UNCALIBRATED
// Raycast: ray-AABB tests/µs (1024 rays × 1024 boxes, multi-core)
static constexpr UINT64 GAME_REF_RAYCAST_TESTS  = 30000ULL;     // UNCALIBRATED

// ── Sub-test weights (must sum to 100) ───────────────────────
static constexpr UINT32 GAME_WEIGHT_PHYSICS  = 20;
static constexpr UINT32 GAME_WEIGHT_ENTITY   = 20;
static constexpr UINT32 GAME_WEIGHT_FRUSTUM  = 15;
static constexpr UINT32 GAME_WEIGHT_NOISE    = 15;
static constexpr UINT32 GAME_WEIGHT_PARTICLE = 15;
static constexpr UINT32 GAME_WEIGHT_RAYCAST  = 15;
