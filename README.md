# RaftKV — Distributed In-Memory Key-Value Store

Fault-tolerant key-value store implementing **Raft consensus from scratch** — leader election, log
replication, and crash recovery across a multi-node cluster — with lock-free read paths and a custom
allocator to limit fragmentation.

**Stack:** C++ · gRPC · POSIX Threads
**Resume target:** `Bravim_Purohit_SDE.tex` → Projects & Publications
**Role:** Software Development Engineer

---

## The claim this repo must prove

> Fault-tolerant key-value store implementing Raft consensus from scratch — leader election, log
> replication, and crash recovery across a multi-node cluster — with lock-free read paths and a custom
> allocator to limit fragmentation. Sustained **[XX]K+** concurrent ops/sec at **[X] ms** p99 read
> latency in load testing.

Every line of that bullet is a contract. If the code doesn't do it, the bullet comes out of the resume.

## Benchmarks this repo owes the resume

The resume ships with bracketed placeholders. They stay bracketed until this table is filled from a
real run on real hardware — no estimates, no extrapolation.

| Metric | Resume placeholder | Measured | Method |
| --- | --- | --- | --- |
| Throughput | `[XX]K+` concurrent ops/sec | — | TBD |
| Read latency | `[X] ms` p99 | — | TBD |

Record for each run: node count, hardware, key/value size, read:write mix, client concurrency, and
whether reads went through the leader or a follower. A throughput number without that context is
unfalsifiable, and an interviewer will notice.

**Do not uncomment** the GitHub link at `Bravim_Purohit_SDE.tex:133` until this table is complete and
the repo is public.

## Architecture

```
        ┌──────────── clients (gRPC) ────────────┐
        │                                        │
        ▼                                        ▼
   ┌─────────┐        AppendEntries        ┌─────────┐
   │ LEADER  │ ──────────────────────────► │FOLLOWER │
   │         │ ◄────────── ack ─────────── │         │
   └────┬────┘                             └────┬────┘
        │  RequestVote / heartbeat              │
        ▼                                       ▼
   ┌──────────────────────────────────────────────────┐
   │  per-node:  log  →  state machine  →  memtable   │
   │             ↑ persisted for crash recovery       │
   └──────────────────────────────────────────────────┘
```

Three layers, deliberately separable:

1. **Consensus** — election, log replication, commit index, term handling. Independent of storage.
2. **Storage** — in-memory memtable behind a custom slab allocator; lock-free read path.
3. **Transport** — gRPC for both peer RPC and the client API.

## Getting started

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Layout

```
src/          consensus, storage, transport
include/      public headers
tests/        unit + cluster integration tests
bench/        load generator and benchmark harness
docs/STUDY.md notes from the reference implementations
```

## Documents

| File | What it's for |
| --- | --- |
| [SPEC.md](SPEC.md) | **Authoritative** technical specification — what to build, the data model, the measurement protocol, and the honest-claims register |
| [ROADMAP.md](ROADMAP.md) | Build order, milestone by milestone |
| [CLAUDE.md](CLAUDE.md) | Operating rules for a coding session here: environment, ports, conventions, when to stop and ask |
| [docs/STUDY.md](docs/STUDY.md) | What to read in the reference implementations before writing code |

Where `SPEC.md` and any other document disagree, `SPEC.md` wins.

## Status

Scaffold — specified, not yet implemented. This repo reserves ports **7100–7199**; up to eight sibling
projects may run at the same time, so nothing here binds outside that block.
