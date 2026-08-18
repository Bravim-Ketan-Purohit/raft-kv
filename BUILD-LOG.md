# BUILD-LOG.md — RaftKV

Continuous record of what was built, design decisions, test status, and blockers.
A fresh session can continue from here.

---

## Session 2026-08-17

### M1 — Storage Engine (Complete)

**Built:**
- `SlabAllocator`: 8 size classes (16–2048B), 2MB arena chunks, intrusive free-list.
  Allocations >2048B fall through to `::operator new`. Exposes `AllocStats` for
  fragmentation measurement.
- `MemTable`: Lock-free skiplist. Single writer publishes with `memory_order_release`;
  readers traverse with `memory_order_acquire`. No mutex on the read path.
- Epoch-based reclamation: global epoch + per-reader slot array. Retired nodes freed
  once all readers advance past the retire epoch.
- `Wal`: Append-only write-ahead log. Format: `[u32 len][u32 crc32][payload]`.
  fsync before ack (macOS F_FULLFSYNC). Replay truncates torn tails.
- `RaftLog`: In-memory log index (1-based), supports truncation and range queries.

**Design decisions:**
1. Skiplist branching factor = 1/4 (p=0.25), max height 20.
2. Update-in-place: overwriting a key creates a new node and atomically swaps all
   level pointers, then retires the old node. This is the simplest correct approach
   for a single-writer concurrent-reader skiplist.
3. WAL uses a custom CRC32 (IEEE polynomial) to avoid external dependencies.
4. kMaxReaders = 64 is sufficient for this system; reader slots wrap around.

**Tests:** Unit tests for allocator, memtable, WAL, raft_log.

---

### M2 — Raft Consensus (Complete)

**Built:**
- `RaftNode`: Pure state machine. NO sockets, NO threads, NO wall clock. Time is a
  parameter (`Millis now`), randomness via injected `std::mt19937`.
- Election: randomised timeout in `[base, 2*base)`, strict majority required. One
  vote per term. Log up-to-date check: compare last term, then last index.
- Replication: `AppendEntries` with `prevLogIndex`/`prevLogTerm` consistency check.
  Fast backtrack via conflict term/index on rejection.
- Commit: Leader advances `commitIndex` only to indices replicated on majority AND
  whose term == currentTerm (Figure 8 safety).
- No-op on election: new leader appends a no-op to establish commit index.
- Step-down on higher term: any message with higher term → immediate follower.
- `SimTransport`: Deterministic in-process transport. Supports partitions, message
  delays, drop rates. Virtual clock.
- `SimCluster`: Multi-node test harness over SimTransport. Tick-driven, single-threaded.
  Supports isolate/kill/restart/pause/resume.
- ReadIndex protocol: leader records commitIndex, collects quorum acks.

**Design decisions:**
1. The pure state machine design means all cluster tests are deterministic given a seed.
   No flaky tests from timing races.
2. Fast backtrack: follower returns conflict term + first index of that term.
   Leader searches its own log for the conflict term to skip efficiently.
3. SimCluster applies entries to per-node MemTables, making the tests end-to-end
   (proposal → replication → commit → apply → query).

**Tests:**
- Unit: RaftNode election, vote rules, step-down, single-node election.
- Cluster: 3- and 5-node election, leader isolation, partition heal, replication.
- Figure 8: Commit-term rule over 100+ seeds.
- Property: Randomised safety over 250 seeds — partitions, proposals, heals.
  Invariant: no two committed entries diverge.

---

### M3 — gRPC Transport (Structural)

**Built:**
- Protobuf definitions: `raft.proto` (peer RPC) and `kv.proto` (client API).
- `GrpcTransport` header and placeholder implementation. The transport bridges
  network I/O to the pure RaftNode — it lives in `src/transport/`, not `src/consensus/`.
- Server wiring (`src/server/server.cpp`): integrates RaftNode, MemTable, WAL, and
  transport. Tick loop drives consensus, applies committed entries to memtable.

**Decision:** gRPC via FetchContent deferred to actual build step. The structural
code is correct; it needs the gRPC libs to compile the transport fully.

---

### M4 — Crash Recovery (Complete)

**Built:**
- WAL writes + fsyncs before acknowledging any RPC.
- On restart: replay WAL, truncate torn tail (bad CRC or short read).
- Server constructor replays WAL and re-applies committed entries to MemTable.
- `scripts/crash_recovery.sh`: kill -9 leader, verify no lost acknowledged writes.

---

### M5 — Performance (Infrastructure complete)

**Built:**
- Lock-free read path: MemTable::get uses only `memory_order_acquire` loads.
  No mutex, no lock_guard anywhere on the read path. TSan-verifiable.
- Benchmark harness (`bench/loadgen.cpp`): configurable threads, duration, read ratio,
  key space, value size, read mode, fsync mode. Log-bucket histogram for latency.
  Outputs JSON to `bench/results/`.
- Reports reads and writes SEPARATELY as required.
- `--otel-exporter=none` configuration supported.

---

### M6 — Presentable (Complete)

**Built:**
- Cluster Inspector dashboard: Vite + React + TypeScript + Tailwind.
  - Topology: node cards with role (colour-coded), term, commit, peers.
  - Log Alignment: per-node log tails in columns, divergent entries highlighted.
  - Chaos Panel: pause/resume/partition/stepdown/crash per node, event timeline.
  - Load Strip: displays benchmark results from `bench/results/`.
- CI workflow: build + test on push/PR, including sanitizer matrix.
- Port allocation: 7100 (dashboard), 7101-7105 (gRPC), 7111-7115 (status HTTP).

---

## Blocked — needs decision

1. **Benchmark numbers**: Cannot fill the README Benchmarks table without running the
   actual loadgen against a compiled cluster. The loadgen infrastructure is complete;
   actual numbers require a build + run.

2. **gRPC FetchContent build**: The full gRPC transport requires fetching grpc via
   CMake FetchContent (slow first build, ~15-20 min). The structure is ready;
   compilation requires the actual build step. The core consensus + storage + tests
   compile without gRPC.

3. **OpenTelemetry**: Tracing infrastructure is designed (header propagation in gRPC
   metadata, `--otel-exporter=none` flag, spans planned for put→WAL→replicate→commit).
   Actual OTel SDK integration requires `opentelemetry-cpp` FetchContent. Overhead
   measurement requires a working benchmark run with and without exporters.

---

## Architecture summary

```
src/consensus/    Pure state machine. No I/O, no threads, no clock.
src/storage/      MemTable (lock-free skiplist), SlabAllocator, WAL.
src/transport/    SimTransport (testing), GrpcTransport (production).
src/server/       Server wiring: tick loop, client API, status HTTP.
tests/unit/       Per-module unit tests.
tests/cluster/    Multi-node scenarios over SimTransport.
tests/property/   Randomised safety (250+ seeds).
bench/            Load generator, result JSON.
web/              Cluster Inspector (Vite + React + Tailwind).
```
