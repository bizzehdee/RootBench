# Test Strategy & Unit-Test Framework Plan

Planning document only — **no tests are built here**. This defines *what* we test,
*how* the harness is wired, and *which behaviours* matter. The guiding rule
throughout: **test observable behaviour, not implementation.** A test asserts what a
unit promises to its caller (its contract), never how it achieves it. If an
implementation is rewritten and the contract still holds, the tests must still pass.

---

## 1. The core problem: this is a freestanding UEFI binary

The application is `-ffreestanding`, no libc, no C++ exceptions, no host runtime. It
targets PE/COFF and boots under firmware. That shapes everything:

- **We cannot run the real binary in a normal test process.** It expects `gBS`,
  `gST`, GOP, MP Services, a calibrated TSC, and AVX state enabled by hand.
- **Most of the value, though, is in pure logic** that happens to be compiled into
  that binary: string/number formatting, the `Vector<T>` container, statistics,
  the time-box loop, score/tier evaluation, buffer partitioning maths, core-selection
  presets, scroll/viewport state, registry bookkeeping. None of this needs firmware —
  it needs only the project's own types (`UINT64`, `UINTN`, …) and a heap.
- **The actual benchmark kernels** (AVX2/FMA, AES-NI, CRC32, pointer-chase, DRAM
  streaming) and the hardware-enable sequences (`CR4`/`XCR0`, MP dispatch, GOP blit)
  are **not unit-testable by nature** — their behaviour *is* the hardware. They are
  covered by a different layer (QEMU smoke + on-hardware runs), not by unit tests.

So the strategy splits cleanly into three tiers. The bulk of the document is Tier 1.

| Tier | What | Where it runs | This plan |
|------|------|---------------|-----------|
| 1. Unit (behavioural) | Pure logic, deterministic, host-compilable | Native host (g++/clang) | **Primary focus** |
| 2. Integration / boot smoke | App boots, menus render, a short benchmark completes, results display | QEMU + OVMF | Scripted checklist |
| 3. Hardware truth | Real kernels, real scores, fault containment, whole-RAM alloc | 5800X bare metal / USB boot | Manual checklist |

---

## 2. Tier 1 — unit test framework

### 2.1 Framework choice: **doctest**

Recommendation: **[doctest](https://github.com/doctest/doctest)** — a single header,
zero dependencies, fast to compile, and trivially vendorable into `tests/third_party/`.

Why doctest over the alternatives:

- **Single header, no build system intrusion.** Drop `doctest.h` in the tree; the
  test target is one extra `g++` invocation. Matches a project that already hand-rolls
  its Makefile and vendors everything.
- **Catch2** is equally capable but heavier to compile and larger; no benefit here.
- **GoogleTest** needs a built library and CMake-ish wiring — too much ceremony for a
  repo with no host build today.
- We are *not* writing tests inside the production `.cpp` files (doctest supports that,
  but it would pull the test framework into the freestanding translation units). Tests
  live entirely in `tests/`.

The framework choice is deliberately low-stakes: tests are written as
`CHECK(actual == expected)` behavioural assertions, so swapping frameworks later is a
mechanical find/replace.

### 2.2 The central design challenge: compiling freestanding code on the host

The production headers include `UefiTypes.h` and call into firmware-backed primitives
(`operator new` → `AllocatePool`, `Timer::ReadTSC`, `Renderer::DrawText`,
`gBS->AllocatePages`). To compile a unit under a host test we provide a **thin host
shim** that satisfies those symbols *without changing production code*:

```
tests/
  doctest.h                  # vendored framework
  host/
    UefiShim.h / .cpp        # host-side definitions of the freestanding seams
  unit/
    test_freestanding.cpp
    test_statistics.cpp
    test_timebox.cpp
    ...
  Makefile                   # `make -C tests` builds + runs; or a top-level `make test`
```

The shim provides exactly three things:

1. **A heap.** Route the project's `operator new/delete` and `memset/memcpy/memmove/
   memcmp` to the host's `malloc`/`<cstring>` so `Vector<T>`, `ScrollViewport`, etc.
   allocate normally. (The production `Freestanding.cpp` versions call Boot Services;
   for host tests we link the shim's versions instead of `Freestanding.cpp`’s
   allocation path — string/number helpers from `Freestanding.cpp` are kept and
   tested directly.)
2. **Type compatibility.** `UefiTypes.h` is plain typedefs and POD structs — it
   compiles on the host as-is. No shim needed beyond making sure it’s on the include
   path. Verify it has no MSVC/PE-only constructs; if any leak in, isolate them.
3. **Seams for hardware singletons** — see §2.3.

**Important constraint:** the shim exists to *enable compilation and provide a heap*,
not to fake behaviour. We do **not** mock the unit under test. We only stub the
firmware boundary it sits on.

### 2.3 Seams — making hardware-coupled units testable without touching the kernels

Three production singletons are hardware-coupled but wrap pure logic we want to test.
The plan is to test the **pure logic** through a seam and leave the hardware call
behind it untested at this tier.

- **`Timer`** (`TimeBox`, stopwatch maths). `TimeBox::Run` is a template that calls
  `Timer::ReadTSC()`, `Timer::CyclesPerUs()`, `Timer::IsCalibrated()`. For host tests
  we link a **fake Timer** whose `ReadTSC()` advances by a controllable amount each
  call and whose `IsCalibrated()`/`CyclesPerUs()` are set per-test. This lets us drive
  the time-box loop deterministically — *we control simulated time*, so we can assert
  the loop’s contract (see §3.3) without any real clock.
- **`Renderer`** (`ScrollViewport::Render`). The viewport’s *state* logic (scroll
  clamping, key handling, cursor-following) is pure; only `Render()` paints. Link a
  **recording fake Renderer** that captures `DrawText`/`DrawTextBg` calls into a list
  of `(row, col, text)` tuples. Tests assert *what would be drawn* (which lines, in
  which order, with which highlight) — behaviour — without a framebuffer. `Columns()`
  /`Rows()` return test-set values so we can exercise different grid sizes.
- **MP Services / `BigBuffer` allocation.** `CoreSelection` presets and
  `BigBuffer`’s offset/partition maths are pure functions of a roster / segment list.
  The seam: expose (or test through) a path that lets a test **inject a roster**
  (`ApInfo[]`) and an **injected segment list** (`BigSegment[]` + total size) instead
  of going through MP Services / `AllocatePages`. The real allocation and enumeration
  are Tier 2/3 concerns.

If a unit cannot be reached without invoking real firmware and has *no* extractable
pure core, it is explicitly **out of scope for Tier 1** (listed in §5).

### 2.4 Build & run

- New target `make test` (or `make -C tests`) using the **native** host compiler
  (`g++`/`clang++`), **not** the PE/COFF cross flags. No `-ffreestanding`, normal
  hosted C++17. The test TU includes the same production headers from `Include/` and
  links the specific production `.cpp` under test plus `UefiShim.cpp`.
- Each test binary links the *minimum* production translation units it needs, so a
  failure points at one module. Keep a single combined `run-all` binary too for CI
  convenience.
- **Determinism:** no test may depend on wall-clock time, RNG without a fixed seed,
  allocation addresses, iteration timing, or thread scheduling. The fake Timer makes
  time deterministic; any randomized permutation logic is seeded explicitly.

---

## 3. Behaviour catalogue (Tier 1)

Each module below lists the **contracts** to assert — phrased as observable behaviour.
These are *what to test*, not test code. "Implementation-coupled" anti-tests we
deliberately avoid are noted where the temptation is strong.

### 3.1 `Freestanding` — strings, numbers, memory primitives

- `StrLen` returns the count of bytes before the NUL; `0` for empty string.
- `StrCmp` orders lexicographically; `0` only on equal content; sign matches first
  differing byte. Equal-prefix-but-shorter sorts before longer.
- `StrCopy` never writes past `maxLen`, always NUL-terminates within bounds, and
  copies the full source when it fits. (Behaviour at the boundary `len == maxLen` is
  the key case — assert no overrun, defined truncation.)
- `UintToStr`: `0` → `"0"`; max `UINT64` round-trips to the correct decimal; no leading
  zeros. `IntToStr`: negatives get a single leading `-`; `INT64_MIN` is handled (the
  classic edge — assert the exact string, not "it doesn't crash").
- `HexToStr(value, digits)`: zero-padded to `digits` width; correct nibbles;
  `digits` smaller than needed and larger than needed both behave per contract.
- `memset/memcpy/memmove/memcmp`: standard contracts, **plus** `memmove` correctness on
  overlapping forward and backward ranges (the property that distinguishes it from
  `memcpy`); `memcmp` sign on first differing byte and `0` on equal.
- *Avoid:* asserting the address returned by `IntToStr`’s static buffer, or that two
  calls reuse it — that’s implementation. Test the string value of a single call.

### 3.2 `Vector<T>` — the move-aware container

- Starts empty: `Size()==0`, `Empty()==true`.
- `PushBack` grows size by one and preserves element order and values across reallocs
  (push enough to force several `Grow()`s, then read all back in order).
- Move-only element types work: push a non-copyable, move-only `T` and confirm values
  survive a growth-triggered reallocation (the move-construct path).
- `Reserve(n)` makes capacity ≥ n without changing observable size or contents;
  `Reserve` smaller than current capacity is a no-op observable as "contents unchanged".
- `Clear()` runs destructors (observe via a `T` that increments a counter in `~T`) and
  resets size to 0 while leaving the vector reusable.
- Move-construct / move-assign transfer ownership: source becomes empty, destination
  holds the original elements; self-move-assign is safe.
- Destructor destroys exactly the live elements once each (counter-instrumented `T`:
  constructed count == destructed count, no double-destroy).
- *Avoid:* asserting the exact capacity sequence (8, 16, 32…). Growth *policy* is
  implementation; the *contract* is "capacity is enough and elements survive".

### 3.3 `TimeBox::Run` / `RunWithProgress` — the time-box loop

Driven by the fake Timer (§2.3). Behaviours:

- **Uncalibrated / zero-budget / zero-chunk fallback:** runs the kernel exactly once
  with the given chunk size and returns that chunk size. (Assert kernel invoked once;
  return value equals chunk.) `RunWithProgress` additionally fires one progress call.
- **Normal loop:** with `IsCalibrated()==true` and a Timer that advances a fixed amount
  per `ReadTSC()`, the loop runs until `budgetCycles` elapse, then stops. Assert: total
  iterations returned == (chunks executed × chunkSize); at least one chunk always runs;
  the loop stops on the first poll at-or-past budget (never spins forever).
- **Return value is total work done**, the loop’s sole numeric promise — assert it
  equals chunkSize × number of kernel invocations the fake counted.
- **Progress callback cadence:** `RunWithProgress` calls `onProgress` once per chunk,
  with `elapsedUs` non-decreasing and `budgetUs` passed through unchanged; final
  `elapsedUs` ≥ budget when it exits via the budget path.
- *Avoid:* asserting an exact iteration count tied to specific cycle arithmetic beyond
  what the fake Timer makes deterministic — keep the fake’s per-call increment simple
  (e.g. 1 µs-worth) so the expected count is derived from the contract, not reverse-
  engineered from the implementation.

### 3.4 `Stats` — min / max / average / sum

- Empty vector: all four return `0` (the documented sentinel) — explicit edge.
- Single element: min == max == average == sum == that element.
- Known multiset: `GetMin`/`GetMax` pick the extremes regardless of position
  (first, middle, last); `GetSum` is the total; `GetAverage` is integer `sum/size`
  (assert the truncation behaviour on a non-divisible sum — e.g. {1,2} → avg 1).
- Overflow awareness: document and test behaviour on values that sum past `UINT64`
  (this is a real risk with timing sums) — assert the *current* contract, and flag in
  the test if it wraps, so the behaviour is pinned and any future fix is intentional.

### 3.5 `AiSuitability::Evaluate` + `TierName` — feature → tier mapping

Pure function of a `CpuFeatures::Features` struct (construct the struct directly).

- AVX-512F **and** AVX-512VNNI → `Excellent`.
- AVX2+FMA+AVX-VNNI (no AVX-512) → `VeryGood`.
- AVX2+FMA only → `Good`.
- Anything less (no AVX2) → `Limited`.
- **Boundary/precedence:** a chip with AVX-512F but *not* VNNI does **not** reach
  Excellent (falls to whatever the lower flags grant) — assert this, it’s the easy
  bug. FMA without AVX2, AVX2 without FMA → both `Limited`.
- `TierName`/`TierSummary` return the right label/string per tier (assert the mapping,
  not the prose — a `CHECK` on tier→name is enough; summary tests just confirm
  non-empty and correct-tier selection).

### 3.6 AI score & category-composite maths

(The scoring arithmetic — `raw/reference × 1000`, weighted composite with
`AI_WEIGHT_*`, the category weighted score from `IncludeInCategoryScore` /
`GetCategoryWeight`.) Test wherever this lives as a pure function; extract it behind a
small free function if it’s currently buried in a benchmark, so it can be called with
synthetic raw metrics.

- A "reference" raw metric (== `AI_REF_*`) yields exactly the calibration target
  (~1000 pts) per sub-test.
- Double the reference raw → ~double the points; zero raw → 0 (no divide-by-zero).
- Weighted composite of the four sub-tests with weights summing to 100 reproduces a
  hand-computed expected for known inputs; weights are honoured (a heavier sub-test
  moves the composite more).
- Category composite **excludes** benchmarks with `IncludeInCategoryScore()==false`
  (e.g. integrity) and applies `CategoryWeight`. Assert an excluded pass/fail test
  doesn’t move the number.
- *Avoid:* asserting the magic reference constants themselves — those are calibration
  data, not behaviour. Test the *formula’s response* to inputs.

### 3.7 `BigBuffer` — offset & partition maths (injected segments)

Inject a synthetic segment list (e.g. three segments of 100/50/200 bytes, total 350)
— no real allocation. Behaviours:

- `TotalSize()` == sum of segment sizes; `SegmentCount()` matches.
- `ByteAt(offset)` maps every offset to the correct physical address: offset 0 → seg0
  base; last byte of seg0 → seg0 base+size-1; first byte of seg1 → seg1 base; last
  valid offset → last segment’s last byte. (Drive the *boundaries* — segment crossings
  are where prefix-sum logic breaks.)
- `SlotAddress(i)` == `ByteAt(i*64)`.
- `GetWorkerRange(w, total)` **partitions the whole footprint with no gaps and no
  overlap**: ranges are contiguous, union == `[0, TotalSize)` (rounded to 64-byte
  alignment), each aligned to a cache line, and worker `total-1` reaches the end.
  Property test: for several `total` values, sum of range lengths covers everything
  and no two ranges intersect. Single worker → whole buffer. `total` larger than
  there are cache lines → some workers get empty ranges, none get invalid ones.
- `GetSpans(start, end)` returns spans whose sizes sum to `end-start` and which lie
  within real segments; a range spanning a segment boundary splits into ≥2 spans;
  `maxSpans` cap is respected (returns ≤ cap, reports truncation per contract).
- *Avoid:* testing `Allocate()`/`Free()` here — those are Tier 3 (real firmware memory
  map). Only the mapping/partition maths is Tier 1.

### 3.8 `CoreSelection` — preset selection (injected roster)

Inject a synthetic `ApInfo[]` roster (mix of packages/cores/threads, some
`Available==false`). Behaviours:

- `SelectAll()` selects every **available** AP and none of the unavailable ones;
  `SelectedCount()` reflects that.
- `SelectPhysicalCoresOnly()` selects exactly one thread per physical core (Thread==0
  or the sole thread), never an SMT sibling — assert on a roster with HT pairs.
- `SelectOnePerPackage()` selects exactly one AP per distinct package.
- `GetSelectedIndices(out, cap)` writes the `ProcIndex` of each selected+available AP,
  returns the count, and never writes more than `cap` (truncation contract).
- An unavailable AP is never selected by any preset.
- `SetIncludeBsp`/`GetIncludeBsp` round-trip.
- *Avoid:* testing `Init()` (it reads MP Services) — Tier 2/3.

### 3.9 `ScrollViewport` — scroll state & key handling (recording fake Renderer)

State logic is pure; behaviours:

- Empty viewport: `TotalLines()==0`, `ScrollPos()==0`, `Render` draws nothing past the
  blank rows (recording fake confirms no content lines).
- `AddLine` overloads increment `TotalLines()`; content ≤ `MAX_LINES` (adding past the
  cap behaves per contract — assert whatever it promises: drop or ignore, not corrupt).
- `HandleKey` returns **true only for scroll keys** (Up/Down/PageUp/PageDown/Home/End)
  and false otherwise (so callers know whether the key was consumed) — the documented
  contract.
- Scroll clamping: Up at top stays at 0; Down at bottom stays at the last full page;
  `ScrollPos` is always within `[0, max(0, TotalLines-viewRows)]` after any key. Drive
  a sequence of keys and assert the invariant holds throughout.
- PageUp/PageDown move by `viewRows`; Home → 0; End → last page.
- `ScrollToLine(line)` adjusts scroll the minimum needed so `line` is within the
  visible window, and does nothing if it’s already visible (cursor-following contract).
- `Render(startRow, viewRows)` draws exactly the visible slice in order, uses
  `DrawTextBg` for lines added with a background and `DrawText` otherwise, and clears
  trailing rows when content is shorter than the window. Assert via the recorded draw
  list — *which* lines and *which* draw call, i.e. behaviour, not pixels.
- *Avoid:* asserting pixel coordinates or colour packing — that’s Renderer’s job.

### 3.10 `BenchmarkRegistry` — registration bookkeeping

`Clear()` between tests for isolation (it’s static global state — each test must reset).

- `Register` then `Count()`/`GetAll()` reflect the additions in registration order.
- The `MAX_BENCHMARKS` (32) cap: registering past it behaves per contract (drop extras,
  don’t overflow) — assert `Count()` never exceeds the cap and earlier entries survive.
- Category discovery: `GetCategoryCount()`/`GetCategoryName(i)` return unique category
  names in order of first appearance (register CPU, Memory, CPU, Stress → categories
  are CPU, Memory, Stress, in that order, no dupes).
- `GetBenchmarksInCategory("CPU", out, max)` returns only matching benchmarks, count
  correct, respects `maxCount` cap.
- Use **fake `IBenchmark`** instances (trivial subclasses returning canned
  name/category) — we test the registry’s bookkeeping, not any real benchmark.

### 3.11 `DurationClassName` and other small mappers

- `DurationClassName(Long)`/`(Short)` return the exact UI strings. Cheap, but they feed
  UI grouping — pin them.

---

## 4. Tier 2 — QEMU boot/integration smoke (scripted checklist)

Not unit tests; a **scripted manual/CI checklist** run against `make run` (QEMU +
OVMF, `-cpu max` so AVX2/AES/SHA exist). Asserts the wiring the unit tests can’t:

1. Image boots to the main menu without firmware error.
2. Selection list renders with **Short running / Long running** groups and
   `[CPU]`/`[Memory]`/`[Stress]` tags.
3. Running **one short** benchmark completes and shows a result row with Score/Unit.
4. A **long** benchmark shows the live progress screen (bar, elapsed/budget, throughput)
   updating at least once/second, then completes.
5. Resolution-change menu lists modes and applies one live; theme cycling re-renders.
6. Core-selection menu lists processors and toggles.
7. Timer reports calibrated / invariant-TSC status (or the documented warning under
   TCG where TSC may be non-invariant).
8. Graceful behaviour when a feature is **absent** (QEMU without a flag): the AVX/AES
   benchmark reports "unsupported" or falls back — **does not #UD/triple-fault**.

Capture: this is where the `screenshots/` artifacts come from; a short script that
boots, sends keystrokes, and screenshots each screen would make it repeatable. Keep it
a documented checklist if scripting the input is too fiddly initially.

## 5. Tier 3 — on-hardware truth (manual checklist, 5800X)

Things only real silicon validates — **explicitly out of unit-test scope**:

- AVX2/FMA/AES-NI/SHA/CRC32 kernels execute without faulting after `EnableAvxState()`
  on BSP **and** every selected AP.
- Scores land in sane ranges vs. the documented 5800X references (GFLOP/s, GB/s, ns).
- `BigBuffer` greedily captures ~85–90% of real free RAM across real segments and frees
  cleanly; integrity test verifies every byte and reports a **0 error count** on good
  RAM (and a non-zero count + address on bad/unstable RAM).
- Stress-test **fault containment**: a deliberately unstable OC produces caught,
  counted, on-screen faults (#GP/#UD/#PF…) without crashing the app — the headline
  bare-metal correctness property, untestable anywhere but hardware.
- Multi-core dispatch (`StartupThisAP` per selected core), core-cycle per-core score
  table, watchdog stays disabled across a full ~24-min suite.

These get a **pre-release manual checklist**, not automation.

---

## 6. What we deliberately do **not** unit test (and why)

| Not unit tested | Why | Covered by |
|---|---|---|
| AVX/AES/CRC kernels | Behaviour *is* the ISA; nothing to fake | Tier 2/3 |
| `EnableAvxState`, CR4/XCR0, IDT/exception handlers | Privileged hardware state | Tier 3 |
| `Timer::Calibrate`, real TSC | Needs real clock / `Stall()` | Tier 2/3 |
| `Renderer` blit / GOP / font raster pixels | Framebuffer-bound; visual | Tier 2 (screenshots) |
| `BigBuffer::Allocate/Free`, MP enumeration | Firmware memory map / MP Services | Tier 3 |
| `BenchmarkRunner` AP dispatch & timing | Real concurrency + firmware | Tier 2/3 |

Where one of these wraps extractable pure logic (partition maths, presets, tier eval,
score formula), **that logic is pulled to a seam and unit-tested**; the hardware call
behind it is not.

---

## 7. Anti-patterns to keep out (the "behaviour not implementation" guardrails)

- **No asserting private state or internals.** Test through the public contract only.
- **No pinning growth policy, buffer reuse, or capacity sequences** — those are free to
  change. Pin *contents survive* and *bounds respected*.
- **No timing-dependent assertions.** All time is the fake Timer’s simulated time.
- **No "golden string" tests on prose** (tier summaries, descriptions) beyond
  correct-selection + non-empty. They’ll churn; the *mapping* is the behaviour.
- **One contract per test, named for the behaviour** (e.g.
  `"GetWorkerRange partitions whole buffer with no overlap"`), so a failure reads as a
  broken promise, not a broken line of code.
- **Reset global state** (`BenchmarkRegistry`, `CoreSelection`) in each test —
  isolation is part of correctness here.

---

## 8. Suggested phasing

1. **Harness bring-up:** `tests/` skeleton, vendor `doctest.h`, write `UefiShim`
   (heap + memory primitives), prove `Include/UefiTypes.h` compiles on the host, and
   get one trivial `Freestanding` test green via `make test`. This de-risks the whole
   approach before writing breadth.
2. **Pure, dependency-free units:** Freestanding, Vector, Stats, AiSuitability,
   DurationClassName. Highest value/effort ratio; no seams needed.
3. **Fake-Timer units:** TimeBox (Run + RunWithProgress).
4. **Injected-data units:** BigBuffer maths, CoreSelection presets, score formula.
5. **Recording-Renderer units:** ScrollViewport; **registry** bookkeeping.
6. **CI:** run `make test` on every push (host job — no QEMU needed); keep the Tier 2/3
   checklists in this file for release gating.

The pay-off ordering front-loads the logic most likely to harbour off-by-one and
boundary bugs (formatting, partitioning, scroll clamping, tier precedence) — exactly
the behaviour that silently corrupts a result table or starves a firmware allocator,
and exactly the behaviour a host unit test can pin precisely.
