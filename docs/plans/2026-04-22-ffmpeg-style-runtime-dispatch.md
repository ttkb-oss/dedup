# FFmpeg-Style Runtime Dispatch for Hashing and Exact Comparison

> For Hermes: Use subagent-driven-development skill to implement this plan task-by-task.

Goal: Add a one-time runtime capability probe and dispatch table so dedup can choose the best hashing and exact-comparison algorithm per workload on Apple Silicon and other targets.

Architecture: Follow the FFmpeg pattern: detect capabilities once, cache them, allow forced overrides for testing, bind function pointers at init, and use measured crossover thresholds instead of branching inside hot loops. Keep correctness strict: hashes are filters only; only exact compare authorizes dedup.

Tech Stack: C23/C2x, macOS sysctl feature probing, optional Metal path later, existing Makefile build, Check test suite.

---

## Desired end state

1. A new runtime capability layer discovers:
- CPU ISA features
- platform traits like unified memory / Metal availability
- measured throughput for candidate compare/hash backends

2. A new dispatch table selects:
- fast candidate hash
- strong staged hash
- exact compare for small inputs
- exact compare for large inputs
- optional batch/stream compare backend

3. dedup.c uses dispatch functions, not hard-coded direct calls.

4. Forced overrides exist for CI, debugging, and benchmarking.

5. Exact proof remains mandatory before deduplication.

---

## Proposed files

Create:
- runtime_caps.h
- runtime_caps.c
- runtime_dispatch.h
- runtime_dispatch.c
- test/runtime_dispatch_suite.c
- test/runtime_dispatch_suite.h

Modify:
- signature.h
- signature.c
- sig_table.c
- dedup.c
- Makefile
- test/Makefile
- test/dedup_check.c

Later/optional:
- runtime_metal_compare.h
- runtime_metal_compare.m
- runtime_bench.c

---

## Public API sketch

### runtime_caps.h

```c
#ifndef __DEDUP_RUNTIME_CAPS_H__
#define __DEDUP_RUNTIME_CAPS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct DedupRuntimeCaps {
    bool apple_arm64;
    bool neon;
    bool dotprod;
    bool i8mm;
    bool crc32;
    bool pmull;
    bool sha3;
    bool unified_memory;
    bool metal_available;

    double memcmp_gib_s_4k;
    double memcmp_gib_s_64k;
    double memcmp_gib_s_1m;
    double memcmp_gib_s_8m;
} DedupRuntimeCaps;

const DedupRuntimeCaps* dedup_runtime_caps_get(void);
void dedup_runtime_caps_reset_for_tests(void);

#endif
```

### runtime_dispatch.h

```c
#ifndef __DEDUP_RUNTIME_DISPATCH_H__
#define __DEDUP_RUNTIME_DISPATCH_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*dedup_fast_hash_fn)(const void* data, size_t len);
typedef bool (*dedup_exact_compare_fn)(const char* a_path, const char* b_path);

typedef struct DedupRuntimeDispatch {
    const char* fast_hash_name;
    const char* strong_hash_name;
    const char* exact_small_name;
    const char* exact_large_name;

    dedup_fast_hash_fn fast_hash;
    dedup_fast_hash_fn strong_hash;
    dedup_exact_compare_fn exact_small;
    dedup_exact_compare_fn exact_large;

    size_t exact_large_threshold;
} DedupRuntimeDispatch;

const DedupRuntimeDispatch* dedup_runtime_dispatch_get(void);
void dedup_runtime_dispatch_reset_for_tests(void);

#endif
```

---

## Forced override environment variables

Implement all of these early so tuning is testable:
- DEDUP_FORCE_FAST_HASH=xxhash|rapidhash|komihash|blake3
- DEDUP_FORCE_STRONG_HASH=none|blake3|sha3
- DEDUP_FORCE_EXACT_COMPARE=memcmp|cpu_tiles|gpu_stream
- DEDUP_FORCE_GPU=0|1
- DEDUP_DISABLE_BENCH=0|1

Behavior:
- invalid values print a warning and fall back to auto
- test code should be able to clear/reset caches between runs

---

## Capability detection plan

### Task 1: Add runtime_caps scaffolding

Objective: Introduce a cached capability record with reset support.

Files:
- Create: `runtime_caps.h`
- Create: `runtime_caps.c`
- Test: `test/runtime_dispatch_suite.c`

Step 1: Write failing tests for cache/reset behavior.
Step 2: Implement a static cached record with lazy init.
Step 3: Add reset helper for tests.
Step 4: Run new suite only.
Step 5: Commit.

### Task 2: Add macOS Apple Silicon feature probing

Objective: Probe the same kind of sysctl feature bits FFmpeg uses on aarch64/macOS.

Files:
- Modify: `runtime_caps.c`
- Test: `test/runtime_dispatch_suite.c`

Probe these keys when available:
- `hw.optional.arm.FEAT_DotProd`
- `hw.optional.arm.FEAT_I8MM`
- `hw.optional.armv8_crc32`
- `hw.optional.arm.FEAT_PMULL`
- `hw.optional.armv8_2_sha3`

Also derive:
- `apple_arm64`
- `neon=true` on arm64 Apple Silicon
- `unified_memory=true` on Apple Silicon
- `metal_available` initially by presence heuristic or stub false; refine later

Step 1: Add helper `have_sysctl_u32(name)`.
Step 2: Fill `DedupRuntimeCaps` fields.
Step 3: Add tests that validate override/reset plumbing and non-crash behavior.
Step 4: Run suite.
Step 5: Commit.

### Task 3: Add microbenchmark fields for memcmp baselines

Objective: Capture the CPU baseline that all alternates must beat.

Files:
- Modify: `runtime_caps.c`
- Test: `test/runtime_dispatch_suite.c`

Measure representative buckets:
- 4 KiB
- 64 KiB
- 1 MiB
- 8 MiB

Use equal or late-mismatch buffers so benchmark approximates proof-of-equality cost.

Step 1: Write test that ensures benchmark fields are initialized to positive values or zero when disabled.
Step 2: Add a small internal benchmark helper.
Step 3: Honor `DEDUP_DISABLE_BENCH=1`.
Step 4: Run suite.
Step 5: Commit.

---

## Dispatch table plan

### Task 4: Add runtime_dispatch scaffolding

Objective: Introduce one-time dispatch binding.

Files:
- Create: `runtime_dispatch.h`
- Create: `runtime_dispatch.c`
- Test: `test/runtime_dispatch_suite.c`

Default initial policy:
- fast hash: existing xxhash64 implementation until replacements land
- strong hash: none
- exact small: `files_match_exact`
- exact large: `files_match_exact`

Step 1: Write failing tests for default binding and cache/reset.
Step 2: Implement static dispatch object.
Step 3: Export selector getter.
Step 4: Run suite.
Step 5: Commit.

### Task 5: Add forced override parsing

Objective: Make algorithm choice controllable.

Files:
- Modify: `runtime_dispatch.c`
- Test: `test/runtime_dispatch_suite.c`

Step 1: Write failing tests for `DEDUP_FORCE_*` parsing.
Step 2: Implement environment parsing.
Step 3: Bind named backends.
Step 4: Run suite.
Step 5: Commit.

### Task 6: Add heuristic size thresholds

Objective: Support FFmpeg-style “bind once, choose by size class”.

Files:
- Modify: `runtime_dispatch.h`
- Modify: `runtime_dispatch.c`
- Modify: `signature.c`
- Test: `test/runtime_dispatch_suite.c`

Initial policy:
- `< 64 KiB`: exact_small
- `>= 64 KiB`: exact_large

Keep it simple first. Later tune thresholds from benchmarks.

Step 1: Add wrapper in `signature.c` that routes through dispatch.
Step 2: Add tests that threshold choice is stable.
Step 3: Run suite.
Step 4: Commit.

---

## Hash backend plan

### Task 7: Isolate current xxhash helper behind a backend API

Objective: Stop hard-coding xxhash inside signature construction.

Files:
- Modify: `signature.h`
- Modify: `signature.c`
- Modify: `runtime_dispatch.c`
- Test: `test/signature_suite.c`

Step 1: Write failing tests that current behavior remains unchanged.
Step 2: Rename existing helper into a backend-style function.
Step 3: Route quick hash through dispatch-selected backend.
Step 4: Run signature suite.
Step 5: Commit.

### Task 8: Add candidate replacement backends incrementally

Objective: Make room for better SMHasher3-ranked filters.

Files:
- Modify: `runtime_dispatch.c`
- Maybe create later: `hash_backend_rapidhash.c`, `hash_backend_komihash.c`
- Test: `test/runtime_dispatch_suite.c`

Order:
1. xxhash existing backend
2. placeholder backend names with fallback
3. one real replacement backend at a time

Note: do not land an external backend without updating license/dependency posture.

---

## Exact compare and GPU plan

### Task 9: Split exact compare into explicit backends

Objective: Separate correctness oracle from dispatch choice.

Files:
- Modify: `signature.c`
- Modify: `runtime_dispatch.c`
- Test: `test/signature_suite.c`

Backends:
- `memcmp_exact_compare` (CPU baseline)
- `cpu_tiled_exact_compare` (later)
- `gpu_stream_exact_compare` (stub returns unavailable for now)

Step 1: Extract current exact compare as named CPU backend.
Step 2: Add dispatch wrappers.
Step 3: Keep behavior identical.
Step 4: Run signature suite.
Step 5: Commit.

### Task 10: Add GPU streaming compare stubs with capability gating

Objective: Prepare for SoC GPU acceleration without changing semantics.

Files:
- Modify: `runtime_caps.h`
- Modify: `runtime_caps.c`
- Modify: `runtime_dispatch.c`
- Test: `test/runtime_dispatch_suite.c`

Behavior:
- only selected if available and forced/benchmarked
- for now may remain unimplemented and never auto-selected

This task exists to freeze the API, not to ship Metal yet.

---

## Integration plan

### Task 11: Convert sig_table to dispatch-selected exact compare

Objective: Remove direct coupling to `files_match_exact()`.

Files:
- Modify: `sig_table.c`
- Test: `test/signature_suite.c`

Replace:
- direct call to `files_match_exact()`
With:
- dispatch-selected exact compare chosen by file size threshold

Step 1: Write failing test around same behavior.
Step 2: Swap call site.
Step 3: Run signature suite.
Step 4: Commit.

### Task 12: Surface backend names in verbose/debug output

Objective: Make runtime selection inspectable.

Files:
- Modify: `dedup.c`
- Modify: `runtime_dispatch.c`
- Test: `test/runtime_dispatch_suite.c`

Verbose output example:
- `runtime: fast_hash=rapidhash strong_hash=none exact_small=memcmp exact_large=memcmp threshold=65536`

Step 1: Add getter for names.
Step 2: Print only under verbosity/debug flag.
Step 3: Run tests.
Step 4: Commit.

---

## Test plan

### Required test files

Create/extend:
- `test/signature_suite.c`
  - sub-4-byte files still work
  - sample-only collision rejected
  - dispatch path preserves correctness
- `test/runtime_dispatch_suite.c`
  - cache/reset behavior
  - env override parsing
  - threshold routing
  - benchmark disable path
  - non-crash feature probing

### Commands

New focused command:
```bash
make dedup && cd test && make dedup_check && CK_RUN_SUITE=signature ./dedup_check
```

Extended command once runtime suite is added:
```bash
make dedup && cd test && make dedup_check && CK_RUN_SUITE=runtime_dispatch ./dedup_check
```

Full targeted validation before PR update:
```bash
git diff --cached --check
make dedup
cd test && make dedup_check
CK_RUN_SUITE=signature ./dedup_check
CK_RUN_SUITE=runtime_dispatch ./dedup_check
```

---

## Heuristic policy to start with

Initial auto policy:
- fast hash: existing xxhash backend
- strong hash: none
- exact small: memcmp-based current exact compare
- exact large: memcmp-based current exact compare
- GPU: never auto-selected yet

Second milestone:
- fast hash: rapidhash or komihash if integrated
- strong hash: BLAKE3 optional
- exact large: GPU only when batch size and file size exceed measured threshold

---

## Non-goals for first pass

Do not do these in the first pass:
- no Metal kernel implementation yet
- no external hash dependency import unless explicitly approved
- no per-call dynamic branching in hot loops
- no weakening of exact-compare correctness
- no replacing memcmp just because a probabilistic stage looks strong

---

## Verification checklist

Before calling this done:
- [ ] capability detection is cached and resettable
- [ ] env overrides work
- [ ] dispatch table is bound once
- [ ] sig_table uses dispatch-selected exact compare
- [ ] hashes remain filters only
- [ ] focused suites pass
- [ ] verbose output reveals chosen backends
- [ ] no license contamination from pasted FFmpeg code

---

## Notes specific to this repo

Current relevant locations:
- `signature.c` owns quick-hash and exact-compare logic
- `sig_table.c` decides whether two candidates are equal
- `dedup.c` owns main runtime flow and verbosity output
- `test/signature_suite.c` already contains the collision and small-file regressions we need to preserve

This plan should be implemented before any Metal compare work so the backend seam exists first.