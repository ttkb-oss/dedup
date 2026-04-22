# Progressive Tiled Verifier Design

> For Hermes: Implement only after the FFmpeg-style runtime dispatch seam exists. Keep exact compare as the only authorization path for dedup.

Goal: Add a staged verifier that uses cheap filters, then stronger tiled witnesses, then a terminal full-coverage exact compare. The design must support CPU-only operation first and GPU streaming later on Apple Silicon unified-memory systems.

Architecture: Treat every probabilistic or compressed stage as a candidate filter only. Stages may reject quickly, but never accept finally. The terminal stage must cover every byte and reduce exact differences without information loss.

Tech Stack: C23/C2x, Apple Silicon runtime dispatch, optional Metal compute backend later, existing runtime capability layer, existing signature/sig_table integration.

---

## Core rule

Only this predicate authorizes equality:

```text
equal(A, B) := OR_reduce_over_all_bytes(A XOR B) == 0
```

Everything else is a screening step.

That means:
- hashes do not prove equality
- sampled overlap does not prove equality
- integer dot products do not prove equality
- floating-point FMA does not prove equality
- only full-coverage exact reduction proves equality

---

## Verifier pipeline

The verifier should be modeled as these stages:

1. Stage 0: metadata filter
- same size required
- same device only if clone semantics require it
- skip empty mismatched metadata immediately

2. Stage 1: cheap candidate filter
- existing signature samples
- fast candidate hash on first prefix window
- optional tail window hash

3. Stage 2: progressive tiled witnesses
- tiled overlap windows
- integer projection / polynomial witnesses per tile
- may run on CPU vector path or GPU batched path

4. Stage 3: terminal exact verify
- full byte coverage
- XOR each lane
- OR-reduce mismatch lanes across all tiles
- equal only if final reduction mask is zero

Stages 1 and 2 may only reject. Stage 3 may reject or accept.

---

## Mathematical model

### Exact proof primitive

For files represented as byte vectors `a[i]` and `b[i]`:

```text
d[i] = a[i] XOR b[i]
D = OR_i d[i]
A == B iff D == 0
```

Equivalent vectorized form per tile:

```text
lane_diff = xor(vec_a, vec_b)
tile_mask = or_reduce(lane_diff)
file_mask = OR over all tile_mask
```

This is the only terminal reduction allowed to certify equality.

### Progressive witness primitive

For a tile `t` of bytes, build one or more exact integer witnesses:

```text
w_k(t) = sum_i coeff_k[i] * byte_i mod 2^64
```

Compare witnesses between candidate files:

```text
match_k(t) = (w_k(A_t) == w_k(B_t))
```

Properties:
- useful for fast rejection
- can make false accepts extremely rare
- still not proof because information is compressed

### Polynomial / PMULL witness primitive

A GF(2)-style tile witness can be computed as:

```text
w(t) = fold_pmull(tile_bytes, tile_seed)
```

This is a strong filter for Apple Silicon because PMULL exists on this machine.

### Overlap discipline

Use overlap so local edits are less likely to be hidden by boundary alignment.

For tile size `T` and stride `S`, require:
- `S < T`
- recommended start: `T = 256 KiB`, `S = 128 KiB`

This means every interior byte participates in at least two witness tiles.

Important: overlap increases witness strength but still does not replace terminal exact coverage.

---

## CPU verifier design

### Stage 2 CPU backend: cpu_tiles

Purpose:
- strong rejection path for medium and large files
- no GPU dependency
- built from NEON, PMULL, DOTPROD, I8MM where available

Suggested backends by hardware:
- scalar fallback
- NEON XOR/OR exact backend
- PMULL witness backend
- DOTPROD witness backend
- I8MM witness backend only if it materially helps byte-lane accumulation

### CPU tiled witness layout

Per tile compute:
- prefix fast hash of tile
- tail fast hash of tile
- 2-4 independent integer witnesses
- optional polynomial witness

Recommended witness set on Apple Silicon:
- witness0: 64-bit sum of bytes weighted by lane index
- witness1: 64-bit sum of bytes weighted by seed-derived coefficients
- witness2: PMULL-based polynomial fold
- witness3: optional second PMULL fold with independent seed

Reject immediately if any witness differs.
If all witnesses match, advance to terminal exact stage.

### CPU terminal exact backend

Do not call this “memcmp” in the design unless it really is libc memcmp.
Model it as:
- load vector lane
- XOR
- accumulate OR into a running mismatch register
- after each chunk, if mismatch register != 0 return false
- if all chunks completed with zero mismatch, return true

This allows later swapping between:
- libc memcmp
- manual NEON XOR/OR
- CPU tiled exact compare

### CPU exact compare pseudocode

```c
bool cpu_exact_xor_or(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8x16_t acc = vdupq_n_u8(0);
    size_t i = 0;

    for (; i + 16 <= len; i += 16) {
        uint8x16_t va = vld1q_u8(a + i);
        uint8x16_t vb = vld1q_u8(b + i);
        acc = vorrq_u8(acc, veorq_u8(va, vb));
    }

    if (vmaxvq_u8(acc) != 0)
        return false;

    for (; i < len; i++) {
        if ((a[i] ^ b[i]) != 0)
            return false;
    }
    return true;
}
```

---

## GPU verifier design

### When GPU is worth using

On this M3 Pro we measured libc memcmp near 48 GiB/s for large equal buffers. GPU should only be selected when batching and unified memory amortize dispatch overhead.

Initial policy targets GPU only when all of these are true:
- unified memory available
- Metal available
- file size >= 8 MiB
- candidate batch count >= 16
- size bucket is homogeneous enough to tile efficiently

Never auto-select GPU for:
- tiny files
- one-off candidate pairs
- latency-sensitive single comparisons

### GPU backend split

1. gpu_witness_stream
- computes strong overlapped tile witnesses for many pairs
- returns per-pair reject/pass-to-exact flags

2. gpu_exact_stream
- computes XOR/OR reduction over all tiles
- returns equality bit only
- must be exact, not probabilistic

The first backend is optional; the second is the backend that can actually authorize equality.

### GPU memory model on Apple Silicon

Prefer:
- shared / managed buffers in unified memory
- command-buffer batching over many pairs
- compact per-pair descriptor tables

Avoid:
- re-packing file contents into large transient staging buffers unless measurement proves it wins
- per-pair command buffer submission

### Pair descriptor sketch

```c
typedef struct DedupComparePairDesc {
    const uint8_t* a;
    const uint8_t* b;
    uint64_t len;
    uint64_t pair_id;
    uint64_t tile_size;
    uint64_t tile_stride;
} DedupComparePairDesc;
```

### GPU witness kernel shape

Each threadgroup handles one tile of one pair.

Inputs:
- base pointer A
- base pointer B
- tile offset
- tile length
- seeds / coefficients

Outputs per tile:
- tile_witness_equal bit
- optional witness values for debugging

Reduction:
- one per-pair bitmask saying whether any tile witness failed

### GPU exact kernel shape

Each threadgroup handles one exact tile of one pair.

Per lane:
- load bytes from A and B
- XOR them
- OR-reduce within SIMDgroup / threadgroup
- write tile mismatch bit

Final reduction:
- CPU or second GPU kernel reduces tile mismatch bits to one pair result
- pair is equal iff all tile bits are zero

This kernel is exact because every byte is covered and only XOR/OR reduction is used.

---

## Tile geometry

### Witness tiles

Initial witness geometry:
- tile size: 256 KiB
- stride: 128 KiB
- overlap: 50%

Why:
- large enough to amortize launch / vector setup
- overlap prevents boundary-only edits from hiding in a single tiling grid
- still manageable for CPU cache streaming and GPU batching

### Exact tiles

Initial exact geometry:
- CPU exact tile: 64 KiB to 1 MiB chunks, backend-tuned
- GPU exact tile: 256 KiB to 1 MiB chunks depending on command-buffer efficiency

Exact tiles do not need overlap because they cover all bytes exactly.

---

## Runtime dispatch policy

This should fit into the existing FFmpeg-style dispatch plan.

### Public backend names

Fast candidate hash:
- `xxhash`
- `rapidhash`
- `komihash`

Strong staged hash:
- `none`
- `blake3`
- `pmull_poly`

Exact compare backends:
- `memcmp`
- `cpu_xor_or`
- `cpu_tiles`
- `gpu_exact_stream`

Witness backends:
- `none`
- `cpu_witness`
- `gpu_witness_stream`

### Suggested dispatch struct extension

```c
typedef bool (*dedup_exact_compare_fn)(const char* a_path, const char* b_path);
typedef bool (*dedup_pair_witness_fn)(const char* a_path, const char* b_path, uint64_t size);

typedef struct DedupRuntimeDispatch {
    const char* fast_hash_name;
    const char* strong_hash_name;
    const char* witness_name;
    const char* exact_small_name;
    const char* exact_large_name;

    dedup_fast_hash_fn fast_hash;
    dedup_fast_hash_fn strong_hash;
    dedup_pair_witness_fn witness;
    dedup_exact_compare_fn exact_small;
    dedup_exact_compare_fn exact_large;

    size_t witness_threshold;
    size_t exact_large_threshold;
    size_t gpu_batch_threshold;
} DedupRuntimeDispatch;
```

### Initial auto policy

On Apple Silicon M3 Pro-like systems:
- `< 64 KiB`: exact_small = memcmp
- `64 KiB .. < 8 MiB`: exact_large = cpu_xor_or or cpu_tiles
- `>= 8 MiB` and batch < 16: exact_large = cpu_xor_or or memcmp
- `>= 8 MiB` and batch >= 16 and Metal available: witness = gpu_witness_stream, exact_large = gpu_exact_stream

### Forced overrides

Extend prior env knobs with:
- `DEDUP_FORCE_WITNESS=none|cpu_witness|gpu_witness_stream`
- `DEDUP_WITNESS_THRESHOLD_BYTES=<n>`
- `DEDUP_GPU_BATCH_THRESHOLD=<n>`
- `DEDUP_TILE_SIZE=<n>`
- `DEDUP_TILE_STRIDE=<n>`

---

## Benchmarking policy

The verifier is not finished until it chooses backends by measured crossover rather than taste.

### Measure at init or cached first use

For each backend class, measure at least:
- 4 KiB
- 64 KiB
- 1 MiB
- 8 MiB
- 64 MiB

For exact backends test:
- equal buffers
- late mismatch buffers
- early mismatch buffers

For GPU test:
- batch counts 1, 8, 16, 64
- homogeneous size buckets only

### Decision rule

Example initial policy:
- if exact backend is slower than memcmp by more than 10% in a bucket, do not auto-select it
- if GPU exact backend only wins at batch >= 16, set `gpu_batch_threshold = 16`
- if witness stage plus exact stage is slower than exact stage alone, skip witness stage for that bucket

This is the same spirit as FFmpeg’s “feature flags plus slow flags” model.

---

## Correctness invariants

These are mandatory:

1. A witness backend may only produce:
- reject
- continue-to-exact

2. An exact backend must be semantically equivalent to bytewise equality.

3. Floating-point reductions may not participate in terminal equality.

4. Tile overlap may increase witness strength but may not substitute for exact coverage.

5. Any backend I/O error must fail closed.

6. Any GPU “equal” result must mean the exact XOR/OR reduction was zero over the full file.

---

## Suggested implementation tasks

### Task A: Extend runtime dispatch types
- add witness backend slot
- add thresholds for witness and GPU batch selection

### Task B: Add CPU exact XOR/OR backend
- keep current `files_match_exact()` as baseline
- add named exact backend API

### Task C: Add CPU witness backend
- implement overlapped tiles
- 2-4 integer witnesses per tile
- PMULL witness on Apple Silicon when available

### Task D: Add benchmark harness for witness vs exact-only
- verify whether witness stage is worthwhile by bucket

### Task E: Add Metal pair descriptor and stub backend
- no kernel yet
- freeze API so later GPU work plugs in cleanly

### Task F: Add Metal exact XOR/OR backend
- exact reduction only
- compare against memcmp baseline and gate by throughput

---

## Testing plan

Add to `test/runtime_dispatch_suite.c`:
- forced witness selection parsing
- threshold routing
- disabled benchmark path
- reset/cached dispatch behavior

Add to `test/signature_suite.c`:
- witness collision does not authorize equality
- exact backend still rejects sample-only collision case
- exact backend still accepts identical small files
- exact backend still works when witness stage is skipped

Later GPU-specific tests:
- GPU exact backend equals CPU exact backend for generated fixtures
- batch-mode routing only triggers when thresholds are met

---

## Notes for this machine

Current observed hardware/runtime facts:
- Apple M3 Pro
- 18-core GPU
- unified memory
- DotProd available
- I8MM available
- CRC32 available
- PMULL available
- SHA3 available
- measured libc memcmp roughly 48 GiB/s for large equal buffers

Implication:
- CPU exact compare is already extremely strong
- GPU path must win on batched large-file throughput, not single-pair latency
- PMULL-backed witness stages are especially attractive here

---

## Bottom line

The correct architecture is not:
- “keep adding stronger hashes until probability is good enough”

It is:
- cheap filter
- strong progressive witness
- exact terminal XOR/OR proof

That preserves correctness while still giving room for CPU vector and GPU streaming acceleration.