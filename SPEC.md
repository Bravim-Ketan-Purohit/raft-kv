# SPEC — RaftKV

**Authoritative technical specification.** `ROADMAP.md` says what order to build in; this says what to
build. Where they disagree, this file wins. If you believe this spec is wrong, say so and stop — do not
silently redesign.

---

## 1. The claim

> Fault-tolerant key-value store implementing Raft consensus from scratch — leader election, log
> replication, and crash recovery across a multi-node cluster — with lock-free read paths and a custom
> allocator to limit fragmentation. Sustained **[XX]K+** concurrent ops/sec at **[X] ms** p99 read
> latency in load testing.

Resume stack string, which the build must match: *C++, gRPC, POSIX Threads*
(`Bravim_Purohit_SDE.tex:130`).

Five things are promised. Each one is a testable deliverable, not a description:

| Promise | What proves it |
| --- | --- |
| Raft from scratch | No consensus library in the dependency list. Election + replication + safety tests. |
| Leader election | Partition test: exactly one leader per term, no split-brain. |
| Log replication | Property test: committed entries never diverge across nodes. |
| Crash recovery | `kill -9` a leader mid-replication; no acknowledged write is lost. |
| Lock-free read path | Read path contains no mutex acquisition. Provable by inspection + TSan. |
| Custom allocator | Fragmentation measured against `malloc` on the same workload. |

## 2. Non-goals

Explicitly out of scope. Do not build these; do not apologise for their absence in the README.

- Multi-Raft / sharding. One replication group.
- Transactions, MVCC, secondary indexes, range scans beyond a simple prefix scan.
- Disk-resident LSM tree. The state machine is **in-memory**; only the Raft log is persisted.
- Dynamic membership change (joint consensus). Cluster size is fixed at launch. Note this in the README.
- Authentication / TLS. Loopback and private-network only.
- Cross-datacenter deployment, or any cloud infrastructure.

Snapshotting is *optional* (M6) and may be replaced by an explicit README note saying why it is absent.

## 3. Architecture

```
                    web/  Cluster Inspector (Vite + React)
                      │  polls /status, subscribes /events, posts /admin/*
     ┌────────────────┴─────────────────────────────────────┐
     ▼                      ▼                      ▼
  node-1 :7101          node-2 :7102          node-3 :7103   (gRPC peer + client API)
  status :7111          status :7112          status :7113   (HTTP/JSON + SSE)
     │                      │                      │
     └──────── AppendEntries / RequestVote ────────┘

  per node:
  ┌──────────────────────────────────────────────────────────────┐
  │ transport (gRPC)  ─┐                                         │
  │                    ├─► RaftNode  (pure state machine)        │
  │ SimTransport      ─┘      │  no clock, no threads, no I/O    │
  │  (tests only)             │                                  │
  │                           ▼ apply(entry)  [single writer]    │
  │                     MemTable (lock-free skiplist)            │
  │                           │                                  │
  │                     SlabAllocator ──► arena chunks           │
  │                                                              │
  │ WAL (append-only, CRC32, fsync-before-ack)                   │
  └──────────────────────────────────────────────────────────────┘
```

### The one design decision everything else depends on

`RaftNode` is a **pure state machine**. It owns no threads, opens no sockets, and never calls
`std::chrono::now()`. Its entire interface is:

```cpp
struct Output { std::vector<Message> messages; std::vector<LogEntry> to_apply; bool persist_needed; };

class RaftNode {
public:
  RaftNode(NodeId self, std::vector<NodeId> peers, PersistentState restored, Rng rng);
  Output tick(Millis now);                       // election / heartbeat timers
  Output step(const Message& in, Millis now);    // inbound peer RPC
  Output propose(const Command& cmd, Millis now);
  RaftStatus status() const;
};
```

Time is a parameter. Randomness is injected. Consequence: the entire consensus layer is testable in one
process with zero sockets and zero sleeps, and every test is deterministic and reproducible from a seed.
**Do not put a socket, a thread, or a wall clock inside `src/consensus/`.** This is the single most
important structural rule in the repo; violating it makes the safety tests flaky and the project
unfinishable.

## 4. Module layout

```
include/raftkv/          public headers only
src/consensus/           RaftNode, log, terms, election, replication   — no I/O
src/storage/             MemTable, SlabAllocator, Wal
src/transport/           GrpcTransport, SimTransport (in-process, deterministic)
src/server/              wiring, client KV service, status/admin HTTP
proto/                   raft.proto, kv.proto
tests/unit/              per-module
tests/cluster/           multi-node scenarios over SimTransport
tests/property/          randomised safety checks with seeds
bench/                   loadgen + result JSON
web/                     Cluster Inspector dashboard
docs/                    STUDY.md, plus design notes you add
```

## 5. Consensus requirements

Persistent (survives restart, written before any RPC is acknowledged): `currentTerm`, `votedFor`,
`log[]`. Volatile: `commitIndex`, `lastApplied`. Leader-only: `nextIndex[]`, `matchIndex[]`.

Required behaviours:

1. **Election.** Randomised timeout in `[electionTimeoutMs, 2 × electionTimeoutMs)`, default base 300 ms,
   drawn from the injected `Rng`. A candidate needs a strict majority. A node grants at most one vote per
   term. A node rejects a vote if the candidate's log is less up to date (compare last term, then index).
2. **Replication.** `AppendEntries` carries `prevLogIndex`/`prevLogTerm`; a follower rejects on mismatch
   and the leader decrements `nextIndex` (fast-backtrack via the follower's returned conflict term is
   allowed and preferred). Conflicting suffixes are truncated, never merged.
3. **Commit.** The leader advances `commitIndex` only to an index replicated on a majority **and** whose
   term equals the leader's current term. This is the Figure-8 safety condition — get it right and write
   a test named for it.
4. **Terms.** Any message with a higher term causes an immediate step-down to follower.
5. **No-op on election.** A new leader appends a no-op entry to establish its commit index before serving
   linearizable reads.

### Read path

Two modes, selected per request:

- **`LINEARIZABLE` (default).** ReadIndex: the leader records `commitIndex`, confirms leadership with a
  quorum heartbeat round, waits for `lastApplied >= readIndex`, then serves from the memtable. No log
  write is required.
- **`STALE_OK`.** Served by any node from local state, no coordination. The response carries the node's
  `commitIndex` so the client can see how far behind it is.

Followers receiving a `LINEARIZABLE` read return `NOT_LEADER` with the current leader's address. The
client library follows the redirect.

### Lock-free means lock-free

Raft applies committed entries serially from **one** thread. That single-writer property is what makes a
lock-free reader path tractable — use it:

- `MemTable` is a skiplist whose `next` pointers are `std::atomic<Node*>`. The apply thread publishes new
  nodes with `memory_order_release`; readers traverse with `memory_order_acquire`. Readers take no lock
  and never block the apply thread.
- Deleted nodes are **not** freed immediately. Use epoch-based reclamation: a global epoch counter,
  per-reader epoch announcement, and deferred free once all readers have advanced past the retire epoch.
  Getting this wrong produces a use-after-free that only shows under load, so write the epoch test first.
- The read path must be verifiable by inspection: no `std::mutex`, `std::lock_guard`, or
  `pthread_mutex_*` anywhere reachable from `MemTable::get`. Run the cluster tests under
  `-fsanitize=thread` in CI and keep them clean.

### Allocator

`SlabAllocator`: size classes 16, 32, 64, 128, 256, 512, 1024, 2048 bytes, carved from 2 MB arena chunks
requested from the OS. Per-class intrusive free list. Allocations over 2048 bytes fall through to
`::operator new` and are counted separately. Single-writer, so no thread caching and no locks.

It must expose accounting, because the claim is about fragmentation:

```cpp
struct AllocStats { size_t bytes_requested, bytes_mapped, bytes_live, bytes_free_listed; size_t chunks; };
```

Report `bytes_mapped / bytes_live` for `SlabAllocator` vs. a `malloc`-backed control on an identical
insert/overwrite/delete workload. macOS has no `mallinfo`; use RSS delta
(`task_info` / `TASK_BASIC_INFO`) plus the allocator's own counters, and say in the README which numbers
came from which source.

### Durability

WAL record: `[u32 len][u32 crc32][payload]`. Written and `fsync`ed before the node acknowledges any
`AppendEntries` or client write. `--no-fsync` exists for benchmarking only and every benchmark result
must record which mode produced it. On startup: replay, and truncate a torn tail (bad CRC or short read)
rather than refusing to boot.

## 6. Interfaces

### gRPC (`proto/`)

```proto
service RaftPeer {
  rpc RequestVote     (VoteRequest)   returns (VoteReply);
  rpc AppendEntries   (AppendRequest) returns (AppendReply);
  rpc InstallSnapshot (stream Chunk)  returns (SnapshotReply);   // M6, optional
}

service Kv {
  rpc Get    (GetRequest)    returns (GetReply);      // consistency: LINEARIZABLE | STALE_OK
  rpc Put    (PutRequest)    returns (PutReply);
  rpc Delete (DeleteRequest) returns (DeleteReply);
  rpc Scan   (ScanRequest)   returns (stream KeyValue);  // prefix only
}
```

Every reply carries `leader_hint` so a misdirected client can retry without a discovery round trip.

### Status / admin HTTP (per node)

`GET /status` → the object the dashboard renders:

```json
{ "id": 1, "role": "leader", "term": 7, "votedFor": 1, "commitIndex": 4210,
  "lastApplied": 4210, "logLen": 4211, "leaderId": 1, "uptimeMs": 91234,
  "logTail": [{"index":4209,"term":7,"op":"put","key":"k9"}],
  "peers": {"2":{"nextIndex":4211,"matchIndex":4210,"lastAckMs":18}},
  "alloc": {"bytesMapped":8388608,"bytesLive":6120000},
  "readMode": "LINEARIZABLE" }
```

`GET /events` → SSE. One event per state transition (role change, term change, commit advance, peer
timeout). This is what makes elections visible in the UI.

Admin endpoints, **only when the node is started with `--enable-admin`** (default off):
`POST /admin/pause`, `/admin/resume`, `/admin/partition {"from":[2,3]}`, `/admin/stepdown`,
`/admin/crash`. These are chaos controls for the demo; gate them so the repo never reads as if a
production server ships a remote-kill endpoint.

## 7. Cluster Inspector (`web/`)

Vite + React + TypeScript + Tailwind. No backend of its own — it talks to the nodes' status ports
directly. It exists because Raft is invisible otherwise: a recruiter or interviewer can watch an election
happen instead of reading about one.

Screens:

1. **Topology.** One card per node: role (colour-coded), term, commitIndex/lastApplied, log length,
   last-ack age per peer. Live via SSE, 250 ms poll fallback.
2. **Log alignment.** Per-node log tails in aligned columns by index, with divergent entries highlighted.
   After a partition heal this visibly shows a follower's conflicting suffix being truncated.
3. **Chaos panel.** Pause / resume / partition / step-down / kill buttons per node, and an event timeline
   underneath so cause and effect sit on one screen.
4. **Load strip.** Reads the newest `bench/results/*.json` and plots ops/sec and p99 over the run.

The dashboard is a demo and observability surface. It is **M6 work** — the last thing built. It must
never be the reason consensus is unfinished, and no consensus code may take a dependency on it.

## 8. Testing requirements

The project is not done when it runs; it is done when these pass.

| Suite | Must contain |
| --- | --- |
| `tests/unit/` | log matching, term handling, vote rules, memtable ops, allocator accounting, WAL torn-tail replay |
| `tests/cluster/` | 3- and 5-node elections; leader isolation → new leader; heal → old leader steps down; no split-brain |
| `tests/property/` | randomised partitions/delays/restarts over N seeds; invariant: no two nodes hold different entries at the same committed index; every acknowledged write is present after recovery |
| `tests/` (figure-8) | a named regression test for the commit-term rule in §5.3 |
| sanitizers | cluster suite green under ASan/UBSan **and** TSan |

Seeds are printed on failure and any failing seed is committed as a fixed test case.

Crash recovery must be **demonstrated**, not asserted: a script that starts a 3-node cluster, drives
writes, `kill -9`s the leader mid-replication, restarts it, and verifies every acknowledged write
survives. Committed under `scripts/`, run in CI if it fits the time budget.

## 9. Benchmark protocol

This is the only source of the numbers in the resume. Nothing else may fill them in.

`bench/loadgen`: C++ gRPC client. Flags: `--nodes`, `--threads`, `--duration`, `--read-ratio`,
`--key-space`, `--value-bytes`, `--read-mode`, `--fsync`. Latency via log-bucket histogram; report p50,
p95, p99, p99.9. Output JSON to `bench/results/<iso8601>.json`.

Every result records, in the file: node count, host CPU model / core count / RAM, build type, fsync mode,
read mode, key-space size, value size, thread count, duration, and whether nodes shared a host.

**Report reads and writes separately.** They differ by orders of magnitude here: a lock-free
ReadIndex read touches memory, while a write costs a quorum round trip plus an `fsync`. A single blended
"ops/sec" number that is 95 % reads is the kind of thing an interviewer takes apart in thirty seconds. On
this hardware (11 cores, 18 GB, all nodes on one host) expect reads in the high tens-to-hundreds of
thousands per second and durable writes in the low thousands. If the measured read number is what fills
`[XX]K+`, the resume bullet must say **read** ops/sec.

Fill the README Benchmarks table from a committed result file, and reference the filename.

## 10. Milestone acceptance criteria

Ordered as in `ROADMAP.md`. A milestone is done when its criterion is mechanically checkable.

- **M1 Storage.** `ctest` green; memtable + allocator unit tests pass; allocator reports stats; CMake
  builds clean with `-Wall -Wextra -Werror`.
- **M2 Consensus.** 3- and 5-node election and replication suites green over `SimTransport`; property
  test green over ≥ 200 seeds; figure-8 test present and passing. **No sockets involved.**
- **M3 Network.** 3-node cluster launches as separate processes; client `get`/`put` works through
  leader redirection; the cluster suite passes over `GrpcTransport` unchanged.
- **M4 Recovery.** WAL fsync-before-ack; replay on restart; the `kill -9` script passes repeatedly.
- **M5 Performance.** Read path free of locks and TSan-clean; `bench/loadgen` produces a committed
  result JSON; **README Benchmarks table filled from it.**
- **M6 Presentable.** Dashboard renders a real election; README diagram matches the code; snapshotting
  implemented or explicitly declared out of scope; CI green.

## 11. Honest-claims register

Before `Bravim_Purohit_SDE.tex:133` is uncommented, every row must be true.

| Claim in the bullet | Status | Backed by |
| --- | --- | --- |
| Raft implemented from scratch | ☐ | no consensus dependency; safety suites |
| Leader election | ☐ | `tests/cluster/election_*` |
| Log replication | ☐ | property test, ≥ 200 seeds |
| Crash recovery | ☐ | `scripts/crash_recovery.sh` |
| Lock-free read path | ☐ | inspection + TSan |
| Custom allocator limits fragmentation | ☐ | slab vs. malloc, same workload |
| `[XX]K+` ops/sec | ☐ | `bench/results/…json` |
| `[X] ms` p99 read latency | ☐ | same file, same run |

Unchecked row ⇒ the link stays commented and the number stays bracketed. A bracketed metric on a sent
resume is bad; a fabricated one ends the interview.

---

## 12. Extended stack (added 2026-08-17)

This project deliberately gains the least. A consensus implementation is judged on correctness and on the
absence of anything that isn't consensus; a Raft repo carrying Kafka and Kubernetes reads as a repo whose
author didn't know what the project was about. The lean dependency list here **is** a signal.

### OpenTelemetry

The one addition. Replaces ad-hoc logging in the cluster path with real distributed tracing, which a
multi-node consensus system needs more than most:

- **Spans across nodes.** A client `Put` becomes one trace spanning leader receipt → WAL fsync →
  `AppendEntries` fan-out → per-follower ack → commit advance → apply. Propagate trace context in gRPC
  metadata, so a slow commit is attributable to a specific follower's fsync rather than guessed at.
- **Metrics via the OTel SDK**, exported to Prometheus: term changes, election count, commit latency,
  fsync latency, apply lag, allocator `bytes_mapped`/`bytes_live`, per-peer ack age.
- **Log correlation.** Trace and span ids on every structured log line.
- Collector + Jaeger in `docker-compose.observability.yml`, off by default. `--otel-exporter=none` must be
  a supported configuration, and **the benchmark runs with tracing off** — instrumentation overhead in the
  read path would corrupt the p99 number this project exists to report. Measure the overhead once and
  record it in the README.

C++ dependency: `opentelemetry-cpp` via `FetchContent`, pinned.

### Milestone amendment

- **M5 Performance** additionally requires: measured tracing overhead documented, and benchmark runs
  confirmed to have exporters disabled.
- **M6 Presentable** additionally requires: one exported trace of a leader election committed as a
  screenshot or JSON under `docs/`, because the trace is the clearest artefact this project can show.
