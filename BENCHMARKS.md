# Benchmark & Test Results — RaftKV

Recorded 2026-08-17. Host: Apple M3 Pro / 11 cores / 18 GB / macOS 27.0 arm64

## Test suite — PASSING

```
100% tests passed, 0 tests failed out of 316
Total Test time (real) = 2.40 sec
```

Breakdown:

| Suite | Tests | Result |
| --- | --- | --- |
| `tests/unit` | memtable, raft_log, raft_node, slab_allocator, wal | pass |
| `tests/cluster` | election, replication, partition, **figure8** | pass |
| `tests/property` | `RandomizedSafety` over **250 seeds** | pass |

The figure-8 commit-term test and 250 randomized safety seeds are the substantive
results here: committed entries never diverge across nodes under randomized
partitions, delays, and restarts.

## Throughput / latency — NOT MEASURED

`bench/loadgen.cpp` is a **placeholder**. Its hot loop is:

```cpp
std::this_thread::yield(); // Placeholder for actual gRPC call
```

It contains zero references to `SimCluster`, `GrpcTransport`, or gRPC. It measures the
latency of a no-op `yield()` and reports it as "read ops/sec" and "p99 read latency".

**Any number this binary prints is meaningless.** It describes the scheduler, not RaftKV.

Consequence for the resume: `[XX]K+ ops/sec` and `[X] ms` p99 at
`Bravim_Purohit_SDE.tex:131` **cannot be filled** until the loadgen is wired to a real
cluster. Two options:

1. **Drive `SimCluster` directly** — real consensus, log replication, memtable, and
   allocator, minus the network. Honest and cheap; must be reported as
   *in-process cluster, no network*, and cannot support the word "concurrent" since
   SimCluster is single-threaded on a virtual clock.
2. **Drive the gRPC server** — supports the bullet as written, but needs gRPC pulled in
   via FetchContent and the `raftkv_server` path exercised across processes.

## Build fixes applied to get here

The repo did not compile as committed. Four defects:

| File | Defect |
| --- | --- |
| `CMakeLists.txt` | `add_compile_options(-Wall -Wextra -Werror)` was global, so `-Werror` was applied to FetchContent'd **googletest**, which fails on a `char8_t`→`char32_t` conversion warning under Apple clang 17. Moved after `FetchContent_MakeAvailable` so third-party code is exempt. |
| `src/consensus/raft_node.cpp` | dead variable `log_ok`, superseded by `candidate_up_to_date` directly below it |
| `src/transport/sim_transport.cpp` | `nodes_.emplace(id, std::move(ns))` — `NodeState` holds a `MemTable`, which deletes its copy ctor and (having a user-declared destructor) has no implicit move ctor, so `NodeState` is non-movable. Now constructed in place. |
| `src/transport/sim_transport.cpp` | empty replay loop over `state.log` with an unused loop variable |
| `include/raftkv/grpc_transport.h`, `tests/cluster/partition_test.cpp` | unused private field / unused local |

## Open issue found while fixing

`SimCluster::restart()` does not replay committed entries into the memtable. A restarted
node re-derives applied state from the leader via AppendEntries, so this is defensible —
but it means the sim path does not exercise WAL-replay-into-state. Worth a test.
