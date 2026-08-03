# CLAUDE.md — RaftKV

Operating instructions for a Claude Code session in this repo. Read `SPEC.md` before writing code;
follow `ROADMAP.md` for order.

## What this is

A from-scratch Raft consensus implementation backing an in-memory KV store, in C++ with gRPC. It exists
to prove a specific resume bullet (quoted in `SPEC.md` §1). The bullet is the contract: if the code
doesn't do it, the bullet comes off the resume, so under-delivering here is worse than delivering late.

## Hard rules

1. **Stay inside this directory.** This repo is its own independent git repository; its parent directory
   is deliberately *not* a repo, and seven sibling projects live beside it. Never read, write, or `git`
   anything above `raft-kv/`. Never `cd ..`.
2. **Never invent a measurement.** Benchmark numbers come from a committed run in `bench/results/`.
   No estimates, no extrapolation, no "approximately". If it wasn't measured, the table says `—`.
3. **Never touch the resume.** The `.tex` files are in another repo. Do not edit them, and do not
   uncomment the GitHub link. That is a human decision made after the benchmark table is full.
4. **No consensus library.** No etcd/raft, no NuRaft, no braft. Reading NuRaft for reference is
   encouraged (`docs/STUDY.md`); linking it defeats the entire point.
5. **`src/consensus/` has no I/O.** No sockets, no threads, no `chrono::now()`, no logging to disk.
   Time and randomness arrive as parameters. See `SPEC.md` §3.
6. **Don't weaken a test to make it pass.** A flaky safety test means the implementation is wrong.
   If a property test fails, commit the seed as a fixed case and fix the bug.
7. Secrets: none needed. If you ever want an API key here, something has gone wrong.

## Environment (this machine: arm64 macOS, 11 cores, 18 GB)

Apple clang 17 and CMake 4.2 are installed. `ninja` is **not**:

```bash
brew install ninja          # preferred
# or drop -G Ninja and use the default Makefiles generator
```

Build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 10
ctest --test-dir build --output-on-failure
```

Sanitizer builds (required before M5 is done):

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DRAFTKV_SANITIZE=thread
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DRAFTKV_SANITIZE=address,undefined
```

gRPC/protobuf: prefer CMake `FetchContent` over a Homebrew gRPC, so CI and this laptop resolve the same
version. Pin the tag. `FetchContent` for gRPC is a slow first build — that is expected, not a hang.

Dashboard (`web/`, M6 only): Node 22 and npm 10 are installed. `npm ci && npm run dev`.

## Ports — this project owns 7100–7199

Eight sibling projects may be running at the same time. Never bind outside this block, and never bind
:3000, :5432, :6379, or :8080.

| Port | Use |
| --- | --- |
| 7100 | `web/` Cluster Inspector dev server |
| 7101–7105 | node 1–5 gRPC (peer + client API) |
| 7111–7115 | node 1–5 status/admin HTTP + SSE |

## Commands

```bash
./scripts/cluster_up.sh 3          # launch a local 3-node cluster on the ports above
./scripts/crash_recovery.sh        # kill -9 the leader mid-replication, verify no lost writes
./build/bench/loadgen --nodes 3 --threads 8 --read-ratio 0.95 --duration 60s
ctest --test-dir build -R property --output-on-failure
```

Write these scripts as you reach the milestone that needs them; don't stub them early.

## Conventions

- C++20. `-Wall -Wextra -Werror`. No exceptions across module boundaries — return `std::expected`-style
  result types or status codes; exceptions inside a module are fine.
- Headers in `include/raftkv/`, one class per header, `.cpp` in `src/<module>/`.
- `snake_case` for functions and variables, `PascalCase` for types, `kConstant` for constants.
- clang-format (LLVM base, 100 columns). Add `.clang-format` in M1 and keep the tree formatted.
- Tests: GoogleTest. Name safety tests after what they protect —
  `figure8_commit_term_rule`, `no_split_brain_under_partition`.
- Commits: imperative subject, ≤ 72 chars, scope prefix — `consensus: reject vote on stale log`.
  Commit at each green milestone step, not in one giant drop.
- Git identity is already configured for this repo (`bravimpurohit1305@gmail.com`). Don't change it.

## Definition of done, and when to stop

A milestone is done per `SPEC.md` §10 — mechanically checkable, not "looks right". CI must be green on
push; the workflow tolerates a missing `CMakeLists.txt` only until M1, after which a real build runs.

**Stop and ask the user** when:

- A `SPEC.md` requirement looks wrong or unimplementable as written.
- A benchmark result won't support the resume bullet's shape (e.g. write throughput lands two orders of
  magnitude below `[XX]K+`) — that is a resume-wording decision, not a coding decision. Report the real
  numbers and let the user choose.
- You want to add a dependency not already named in `SPEC.md`.
- Work would touch anything outside this directory.

Report honestly. If the property test is failing on 3 of 200 seeds, say exactly that — a known-red
safety test is useful information; a green suite that skips the hard case is a liability.

---

## Extended stack additions (2026-08-17)

See `SPEC.md` §12. This project gains only **OpenTelemetry** — deliberately. A Raft repo carrying Kafka and
Kubernetes reads as one whose author didn't know what the project was about; the short dependency list is a
signal, so don't add to it.

**New ports** (same 7100–7199 block): `7120` Jaeger UI · `7121` OTel Collector gRPC.

**New prerequisites:** `opentelemetry-cpp` via CMake `FetchContent`, pinned.
`docker-compose.observability.yml` for Collector + Jaeger, **off by default**.

**New hard rules:**

8. **Benchmarks run with exporters off.** `--otel-exporter=none` must be a supported configuration, and every
   committed `bench/results/*.json` must record that tracing was disabled. Instrumenting the read path and
   then reporting its p99 measures your exporter.
9. **Measure the tracing overhead once** and record it in the README. "Negligible" is not a measurement.
10. **Trace context propagates in gRPC metadata**, so a slow commit is attributable to a specific follower
    rather than guessed at. That attribution is the whole reason tracing is here.
