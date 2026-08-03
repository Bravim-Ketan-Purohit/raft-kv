# Roadmap — RaftKV

Build order matters here. Raft is unforgiving: if election and log replication aren't correct in
isolation, every bug above them looks like a storage bug. Get consensus right on a single-process
simulated cluster first, then add the network, then add performance work.

## M1 — Single-node storage engine

- [ ] In-memory memtable: sorted structure, `get` / `put` / `delete`
- [ ] Custom slab allocator for entries; measure fragmentation against `malloc`
- [ ] Unit tests for the storage layer alone, no consensus involved
- [ ] CMake + ctest wired so CI goes green

## M2 — Raft consensus, simulated cluster

- [ ] Node state machine: follower → candidate → leader, with terms
- [ ] Leader election with randomized timeouts; verify no split-brain under partition
- [ ] `AppendEntries` replication + commit index advancement
- [ ] Log consistency check: reject entries whose `prevLogIndex`/`prevLogTerm` don't match
- [ ] Deterministic in-process test harness — inject partitions and delays without real sockets
- [ ] Property test: committed entries never diverge across nodes

## M3 — Real network via gRPC

- [ ] Protobuf definitions for peer RPC (`RequestVote`, `AppendEntries`) and the client API
- [ ] Client-facing `get` / `put` with leader redirection
- [ ] Multi-process cluster launch script (3 and 5 nodes)

## M4 — Crash recovery

- [ ] Persist log + term + vote before acknowledging
- [ ] Replay on restart; rejoin an existing cluster without corrupting the log
- [ ] Kill -9 a leader mid-replication and verify the cluster recovers with no lost commit

## M5 — Performance, then measure

- [ ] Lock-free read path (RCU-style or hazard pointers); reads must not block replication
- [ ] Benchmark harness in `bench/`: configurable concurrency, key/value size, read:write mix
- [ ] Capture throughput and p99 read latency; **fill the Benchmarks table in the README**
- [ ] Record node count, hardware, and workload mix alongside every number

## M6 — Presentable

- [ ] README architecture diagram matches what the code actually does
- [ ] Log snapshotting / compaction (or an explicit note on why it's out of scope)
- [ ] CI green on every push
- [ ] Flip repo public, then uncomment `Bravim_Purohit_SDE.tex:133`

## Gate before the resume link goes live

Both placeholders replaced with measured numbers · crash recovery demonstrated, not asserted · CI
green · README honest about what is and isn't implemented.
