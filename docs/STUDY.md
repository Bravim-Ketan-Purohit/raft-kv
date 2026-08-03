# Study notes — RaftKV

Reference material, carried over from `projects-ref.md`. Read before writing, not instead of writing.

## References

### [`facebook/rocksdb`](https://github.com/facebook/rocksdb)

The industry-standard C++ storage engine.

**What to study:** how they implement their `MemTable` — how data sits in memory before hitting disk.
Look at the skiplist-based default implementation and the arena allocator underneath it. The question
to answer: why an arena instead of per-entry allocation, and what that buys under high write rates.

Relevant to M1 here — the memtable and the custom slab allocator.

### [`eBay/NuRaft`](https://github.com/eBay/NuRaft)

Lightweight C++ Raft consensus implementation. Small enough to read end to end, unlike most
production consensus code.

**What to study:** the `append_entries` API — the core heartbeat keeping distributed nodes in sync.
Trace one call from the leader's send through the follower's consistency check to the commit-index
advance and the ack back.

Relevant to M2 and M3 — replication and the gRPC transport.

## Also worth reading

- **The Raft paper** (Ongaro & Ousterhout, *In Search of an Understandable Consensus Algorithm*).
  Figure 2 is the specification. Implement it literally before improvising.
- **raft.github.io** — the visualizations make election timeouts and split votes concrete.

## Questions to answer before coding

Write the answers down. If you can't answer them, you can't defend the project in an interview.

1. What exactly does Raft guarantee, and what does it *not*? Where does linearizability come from,
   and why do stale reads from a follower break it?
2. Why randomized election timeouts? What happens with fixed ones?
3. Why is the `prevLogIndex` / `prevLogTerm` check enough to guarantee log consistency?
4. What must hit durable storage *before* a node acknowledges, and why does the order matter?
5. How can a read be lock-free while replication mutates the log concurrently?

## Deliberate divergences from the references

Log the places where this implementation intentionally differs — simpler snapshotting, no dynamic
membership, whatever it turns out to be. "I chose not to" is a strong interview answer; "I didn't
know that existed" is not.

| Area | Reference does | This repo does | Why |
| --- | --- | --- | --- |
| | | | |
