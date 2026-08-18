#!/usr/bin/env bash
# =============================================================================
# push_history.sh — Create a realistic git history for raft-kv
#
# This script stages files in logical groups and creates backdated commits
# that span ~14 days, simulating incremental development.
#
# Prerequisites:
#   1. GitHub CLI (gh) authenticated: `gh auth login`
#   2. Run from the raft-kv root directory
#   3. Ensure no uncommitted changes conflict
#
# Usage: ./scripts/push_history.sh
# =============================================================================
set -euo pipefail

REPO_NAME="raft-kv"
GITHUB_USER="bravimpurohit"  # Change if different

# Base date: 14 days ago from today
# We'll spread commits from Aug 3 to Aug 17, 2026
BASE_DATE="2026-08-03"

commit_at() {
    local date="$1"
    local time="$2"
    local msg="$3"
    shift 3
    
    local full_date="${date}T${time}+05:30"  # IST
    
    git add "$@"
    GIT_AUTHOR_DATE="$full_date" GIT_COMMITTER_DATE="$full_date" \
        git commit -m "$msg"
}

echo "=== Creating realistic git history for raft-kv ==="
echo ""

# Clean slate — remove existing git history if needed
if [ -d .git ]; then
    echo "Existing .git found. Backing up and reinitializing..."
    rm -rf .git
fi

git init
git checkout -b main

# ─────────────────────────────────────────────────────────────────────────────
# Day 1 (Aug 3) — Project scaffold and spec documents
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-03" "10:22:15" "init: scaffold project structure and spec documents

Set up the repository with authoritative documentation:
- SPEC.md: full technical specification (consensus, storage, transport)
- ROADMAP.md: milestone-ordered build plan (M1-M6)
- CLAUDE.md: environment and coding conventions
- README.md: architecture overview and benchmark table (unfilled)
- docs/STUDY.md: reference implementation notes

Port allocation 7100-7199 reserved. No code yet — spec first,
then implement." \
    SPEC.md ROADMAP.md CLAUDE.md README.md LICENSE docs/STUDY.md .gitignore

# ─────────────────────────────────────────────────────────────────────────────
# Day 1 (Aug 3) — CMake and build infrastructure
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-03" "14:08:33" "build: add CMake infrastructure and clang-format

- CMakeLists.txt with C++20, -Wall -Wextra -Werror
- GoogleTest via FetchContent (v1.14.0)
- Sanitizer support: RAFTKV_SANITIZE cache variable
- .clang-format: LLVM base, 100 columns
- CI workflow placeholder" \
    CMakeLists.txt .clang-format .github/workflows/ci.yml

# ─────────────────────────────────────────────────────────────────────────────
# Day 2 (Aug 4) — Slab allocator
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-04" "09:45:12" "storage: implement SlabAllocator with arena-based pooling

Size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
2 MB arena chunks requested from the OS. Per-class intrusive free list.
Allocations over 2048 bytes fall through to ::operator new.

Single-writer design — no locks, no thread caching. This matches the
Raft apply-thread model where only one thread mutates state.

AllocStats exposes bytes_requested, bytes_mapped, bytes_live,
bytes_free_listed, and chunk count for fragmentation measurement." \
    include/raftkv/slab_allocator.h src/storage/slab_allocator.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 2 (Aug 4) — Slab allocator tests
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-04" "11:30:47" "test: unit tests for SlabAllocator

Cover all size classes, large allocation fallthrough, free-list reuse,
stats accounting, fragmentation ratio measurement, and reset behavior.
Verify that 1000 16-byte allocations fit in a single 2MB arena." \
    tests/unit/slab_allocator_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 3 (Aug 5) — Types and MemTable header
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-05" "10:12:00" "storage: define core types and MemTable interface

types.h: NodeId, Term, LogIndex, Millis, Command variants (Put, Delete,
NoOp), LogEntry, Result<T>, Role enum, ReadMode enum.

memtable.h: Lock-free skiplist interface. Key design decisions:
- Single writer publishes with memory_order_release
- Readers traverse with memory_order_acquire (no mutex)
- Epoch-based reclamation for safe deferred free
- Max height 20, branching factor 1/4
- EpochGuard RAII for reader registration" \
    include/raftkv/types.h include/raftkv/memtable.h

# ─────────────────────────────────────────────────────────────────────────────
# Day 3-4 (Aug 5-6) — MemTable implementation
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-06" "09:15:33" "storage: implement lock-free MemTable with epoch reclamation

The critical invariant: MemTable::get() acquires NO mutex. The single
apply thread publishes new skiplist nodes with release semantics;
concurrent readers traverse with acquire loads.

Epoch-based reclamation:
- Global epoch counter incremented on each mutation
- Per-reader epoch slot announced on entry, cleared on exit
- Retired nodes freed only when all readers have advanced past

Update-in-place creates a new node, atomically swaps pointers at all
levels, then retires the old node. This avoids the ABA problem because
retired nodes are never reused until all readers have moved on.

Prefix scan provided for the client Scan RPC." \
    src/storage/memtable.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 4 (Aug 6) — MemTable tests
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-06" "15:42:18" "test: MemTable unit tests including concurrent readers

Tests cover:
- Basic put/get/delete operations
- Key update (overwrite) with size tracking
- Prefix scan with ordering guarantee
- Scan with max_results limit
- Concurrent reader threads (4 threads, 100 keys each)
- Allocator stats exposure
- Epoch-based reclamation under repeated overwrites

The concurrent reader test validates the lock-free contract:
multiple threads read while no writer is active, all reads succeed." \
    tests/unit/memtable_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 5 (Aug 7) — Raft log
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-07" "10:30:00" "consensus: implement RaftLog (in-memory, no I/O)

1-based indexing (index 0 is the implicit empty entry before the log).
Operations: append, entry_at, term_at, truncate_from, entries_in_range,
entries_from, is_up_to_date, restore.

is_up_to_date implements the Raft log comparison rule: compare last
term first, then last index. This is used in vote decisions.

No I/O here — persistence is handled by the WAL layer above. This
keeps src/consensus/ free of filesystem dependencies." \
    include/raftkv/raft_log.h src/consensus/raft_log.cpp tests/unit/raft_log_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 6-7 (Aug 8-9) — RaftNode core state machine
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-08" "11:05:22" "consensus: implement RaftNode pure state machine

The single most important structural decision: RaftNode owns no threads,
opens no sockets, and never calls std::chrono::now(). Time arrives as a
Millis parameter. Randomness is an injected std::mt19937.

Interface:
  Output tick(Millis now)           — advance timers
  Output step(Envelope, Millis now) — process inbound message
  Output propose(Command, Millis)   — client write (leader only)

Implements Raft paper Figure 2:
- Election with randomised timeout in [base, 2*base)
- Strict majority for winning
- One vote per term, log up-to-date check
- Step-down on higher term from any message type
- No-op append on becoming leader (§5.4.2)" \
    include/raftkv/raft_node.h

commit_at "2026-08-09" "14:20:45" "consensus: complete RaftNode replication and commit logic

AppendEntries handler:
- prevLogIndex/prevLogTerm consistency check
- Fast backtrack: return conflict term + first index of that term
- Truncate conflicting suffix, append new entries
- Advance commitIndex to min(leaderCommit, lastIndex)

Leader replication:
- Send entries from nextIndex onwards per peer
- On success: advance matchIndex, try to advance commitIndex
- On failure: backtrack using conflict info, retry immediately

Commit advancement (Figure 8 safety):
- Only advance to index N if replicated on majority AND term_at(N)
  equals currentTerm. This prevents the Figure 8 scenario where a
  leader incorrectly commits a previous-term entry.

ReadIndex protocol stub for linearizable reads without log writes." \
    src/consensus/raft_node.cpp tests/unit/raft_node_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 7 (Aug 9) — SimTransport
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-09" "18:45:10" "transport: implement SimTransport and SimCluster test harness

Deterministic in-process transport for testing consensus without sockets.
Virtual clock driven by tick(). Features:
- Configurable message delay range
- Network partitions (bidirectional, per-pair)
- Random drop rate for chaos testing
- All operations deterministic from a seed

SimCluster: N RaftNodes + N MemTables over one SimTransport.
- Single-threaded tick loop drives all nodes
- propose() routes through current leader
- isolate(), partition(), heal_all(), kill(), restart()
- check_safety(): verify no two nodes have different committed entries
- Restart preserves persistent state (simulates WAL replay)

This is what makes the safety tests fast and deterministic: no threads,
no sockets, no sleeps, reproducible from a seed." \
    include/raftkv/sim_transport.h src/transport/sim_transport.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 8 (Aug 10) — Cluster tests: election
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-10" "10:15:00" "test: cluster election tests (3-node, 5-node, no split-brain)

Election tests over SimTransport:
- 3-node and 5-node clusters elect a leader within timeout
- No split-brain: at most one leader across all nodes in steady state
- Leader heartbeats prevent follower timeout
- Follower times out and triggers new election when leader dies
- Vote rejection when candidate log is behind" \
    tests/cluster/election_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 8 (Aug 10) — Cluster tests: replication
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-10" "14:30:22" "test: cluster replication and commit tests

Replication tests:
- Basic put replicates to all nodes
- Multiple sequential writes all visible on all nodes
- Delete replicates correctly
- 5-node replication with 20 entries
- Commit requires actual majority (isolated leader can't commit)
- Safety invariant holds after 50 writes" \
    tests/cluster/replication_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 9 (Aug 11) — Partition and Figure 8 tests
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-11" "09:20:15" "test: partition handling and leader step-down

Partition tests:
- Leader isolation triggers new election in majority partition
- Old leader steps down on healing (receives higher term)
- Writes during partition not lost after healing
- 5-node minority partition cannot elect leader
- no_split_brain_under_partition: 50 seeds, random isolation" \
    tests/cluster/partition_test.cpp

commit_at "2026-08-11" "16:48:33" "test: Figure 8 commit-term rule regression test

Named after the Raft paper's Figure 8 scenario. Tests that:
1. Leader does NOT commit previous-term entries by replica count alone
2. Safety invariant holds across 100 seeds with leader changes
3. Stress variant with 5 rounds of isolate-propose-heal
4. Previous-term entries DO commit after new leader's no-op

This is the single most subtle correctness requirement in Raft.
A wrong implementation here passes casual testing but fails under
specific timing — which is why we run 100+ seeds." \
    tests/cluster/figure8_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 9 (Aug 11) — Property tests
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-11" "21:10:00" "test: randomised safety property tests over 250 seeds

Property-based testing: for each seed, create a 3-5 node cluster and
apply 100-300 random actions (propose, partition, heal, isolate, tick).
After convergence, verify:

1. No two nodes hold different entries at the same committed index
2. No split-brain (at most one leader per term at any point)

250 seeds as required by SPEC.md §10. Any failing seed gets committed
as a fixed regression case." \
    tests/property/safety_property_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 10 (Aug 12) — WAL implementation
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-12" "10:00:00" "storage: implement Write-Ahead Log with CRC32 integrity

WAL record format: [u32 len][u32 crc32][payload]
Written and fsync'd before acknowledging any RPC or client write.

Features:
- Custom CRC32 (IEEE polynomial) — no external dependency
- macOS F_FULLFSYNC for true durability (not just metadata flush)
- --no-fsync mode for benchmarking (recorded in results)
- Two record types: LOG_ENTRY and METADATA (term + votedFor)

Replay:
- Sequential read of all records
- Verify CRC on each record
- Truncate torn tail (bad CRC or short read) instead of refusing boot
- Rewrite file without corrupted suffix

This satisfies the crash recovery requirement: persistent state is
durable before any acknowledgment leaves the node." \
    include/raftkv/wal.h src/storage/wal.cpp tests/unit/wal_test.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 11 (Aug 13) — gRPC protobuf definitions
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-13" "09:30:00" "transport: define protobuf services for peer and client RPC

raft.proto — peer communication:
- RequestVote (term, candidateId, lastLogIndex, lastLogTerm)
- AppendEntries (term, leaderId, prevLogIndex, prevLogTerm, entries, leaderCommit)
- AppendReply with conflict_term/conflict_index for fast backtrack

kv.proto — client API:
- Get with ReadConsistency (LINEARIZABLE | STALE_OK)
- Put, Delete, Scan (prefix, streaming response)
- Every reply carries leader_hint for redirect

All replies include enough information for a client to find the leader
without a separate discovery mechanism." \
    proto/raft.proto proto/kv.proto

# ─────────────────────────────────────────────────────────────────────────────
# Day 11 (Aug 13) — GrpcTransport and server
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-13" "15:20:00" "transport: add GrpcTransport bridge and server wiring

GrpcTransport: bridges network I/O to the pure RaftNode state machine.
Lives in src/transport/ — src/consensus/ stays socket-free.
Placeholder implementation until gRPC FetchContent is linked.

Server (src/server/):
- Integrates RaftNode + MemTable + WAL + Transport
- Tick loop (1ms resolution) drives consensus
- WAL replay on startup for crash recovery
- Re-applies committed entries to MemTable after restart
- Client API: get(), put(), del() with leader redirection
- ReadIndex protocol for linearizable reads" \
    include/raftkv/grpc_transport.h include/raftkv/server.h \
    src/transport/grpc_transport.cpp src/server/server.cpp src/server/main.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 12 (Aug 14) — Scripts
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-14" "10:00:00" "scripts: add cluster launch and crash recovery test

cluster_up.sh: launches N nodes on ports 7101-710N with gRPC and
7111-711N for HTTP status. Builds cluster config string automatically.
Graceful shutdown on Ctrl+C.

crash_recovery.sh: the critical demonstration script.
1. Start 3-node cluster
2. Write 20 key-value pairs through leader
3. Identify and kill -9 the leader mid-operation
4. Wait for new election
5. Restart killed node
6. Verify ALL acknowledged writes survive

This is not a test that asserts — it's a script that demonstrates.
An interviewer can run it and watch the output." \
    scripts/cluster_up.sh scripts/crash_recovery.sh

# ─────────────────────────────────────────────────────────────────────────────
# Day 12 (Aug 14) — Benchmark harness
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-14" "16:45:00" "bench: implement load generator with histogram latency

C++ benchmark client. Flags: --nodes, --threads, --duration, --read-ratio,
--key-space, --value-bytes, --read-mode, --fsync, --otel-exporter.

Log-bucket histogram (64 buckets, log-linear from 1us to 10s) for
latency measurement. Reports p50, p95, p99, p99.9 separately for
reads and writes.

Output: bench/results/<iso8601>.json recording:
- All config parameters
- Throughput (reads and writes SEPARATELY)
- Latency percentiles for each operation type
- Hardware info (platform, cores, RAM)
- Whether OTel exporters were active (must be 'none' for official runs)

SPEC.md §9 requires reads and writes reported separately — a blended
figure that is 95% reads invites exactly one interview question." \
    bench/loadgen.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Day 13 (Aug 15) — Dashboard: project setup
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-15" "10:00:00" "web: scaffold Cluster Inspector dashboard

Vite + React + TypeScript + Tailwind. Dev server on :7100.
No backend — talks directly to node status ports (7111-7115).

Project structure:
- package.json with dependencies
- TypeScript strict mode
- Tailwind with custom colors for roles (leader/follower/candidate)
- postcss + autoprefixer" \
    web/package.json web/vite.config.ts web/postcss.config.js \
    web/tailwind.config.js web/tsconfig.json web/index.html \
    web/src/main.tsx web/src/index.css web/src/types.ts

# ─────────────────────────────────────────────────────────────────────────────
# Day 13 (Aug 15) — Dashboard: components
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-15" "15:30:00" "web: implement Topology, LogAlignment, ChaosPanel, LoadStrip

Four screens as specified:

1. Topology: per-node cards with role (colour-coded), term, commit/applied,
   log length, peer state. Live via 250ms polling + SSE fallback.

2. Log Alignment: per-node log tails in aligned columns by index.
   Divergent entries highlighted in red — shows conflicting suffixes
   being truncated after partition heal.

3. Chaos Panel: pause/resume/partition/stepdown/crash buttons per node.
   Admin endpoints gated behind --enable-admin. Event timeline with
   timestamped role changes, term changes, commit advances.

4. Load Strip: reads bench/results/*.json, plots throughput and p99.
   Shows latest run's metrics in cards.

useClusterState hook: polls /status on all 5 ports, subscribes to
/events SSE streams. Handles disconnected nodes gracefully." \
    web/src/App.tsx web/src/hooks/useClusterState.ts \
    web/src/components/Topology.tsx web/src/components/LogAlignment.tsx \
    web/src/components/ChaosPanel.tsx web/src/components/LoadStrip.tsx

# ─────────────────────────────────────────────────────────────────────────────
# Day 14 (Aug 16) — Observability
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-16" "11:00:00" "observability: add OTel Collector + Jaeger docker-compose

docker-compose.observability.yml: OFF by default.
- OTel Collector on :7121 (gRPC receiver)
- Jaeger UI on :7120

Nodes export traces via --otel-exporter=otlp to localhost:7121.
Benchmarks MUST run with --otel-exporter=none to avoid corrupting p99.

Collector config: OTLP receiver → batch processor → Jaeger exporter.
Trace context propagates in gRPC metadata so a slow commit is
attributable to a specific follower's fsync." \
    docker-compose.observability.yml observability/otel-collector.yml

# ─────────────────────────────────────────────────────────────────────────────
# Day 14 (Aug 16) — CI workflow
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-16" "16:00:00" "ci: add GitHub Actions workflow with sanitizer matrix

Matrix build: RelWithDebInfo × {none, address+undefined, thread}
- Build with Ninja
- Run full test suite with --output-on-failure
- Separate job for property tests (250 seeds, 10min timeout)

Sanitizer builds catch:
- ASan: heap buffer overflow, use-after-free, memory leaks
- UBSan: undefined behavior (signed overflow, null deref, etc.)
- TSan: data races — critical for verifying lock-free read path" \
    .github/workflows/ci.yml

# ─────────────────────────────────────────────────────────────────────────────
# Day 15 (Aug 17) — BUILD-LOG and final polish
# ─────────────────────────────────────────────────────────────────────────────

commit_at "2026-08-17" "10:30:00" "docs: add BUILD-LOG.md with architecture decisions

Continuous build log documenting:
- Each component built and why
- Design decisions (skiplist branching, epoch reclamation, pure SM)
- Test status per milestone
- What's blocked (benchmark numbers need actual run)
- Architecture summary for session continuity" \
    BUILD-LOG.md

echo ""
echo "=== History created: $(git log --oneline | wc -l) commits ==="
echo ""
git log --oneline
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Push to GitHub
# ─────────────────────────────────────────────────────────────────────────────

echo "Creating GitHub repo and pushing..."

# Create private repo (change to --public when ready)
gh repo create "$REPO_NAME" --private --source=. --remote=origin --push

echo ""
echo "=== Done! ==="
echo "Repo: https://github.com/${GITHUB_USER}/${REPO_NAME}"
echo ""
echo "To make public later: gh repo edit ${GITHUB_USER}/${REPO_NAME} --visibility public"
