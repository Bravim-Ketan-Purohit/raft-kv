#!/usr/bin/env bash
# Crash recovery test: starts a 3-node cluster, writes data, kill -9 the leader,
# restarts it, and verifies no acknowledged writes are lost.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DATA_DIR="/tmp/raftkv_crash_test"
NUM_NODES=3
PIDS=()
PASS=true

cleanup() {
    echo "Cleaning up..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -rf "$DATA_DIR"
}
trap cleanup EXIT INT TERM

echo "=== RaftKV Crash Recovery Test ==="
echo ""

# Clean slate
rm -rf "$DATA_DIR"

# Build cluster config
CLUSTER_CONFIG="1=127.0.0.1:7101,2=127.0.0.1:7102,3=127.0.0.1:7103"

start_node() {
    local id=$1
    local port=$((7100 + id))
    local status_port=$((7110 + id))
    local node_dir="${DATA_DIR}/node${id}"
    mkdir -p "$node_dir"

    "${PROJECT_DIR}/build/raftkv_server" \
        --node-id "$id" \
        --port "$port" \
        --status-port "$status_port" \
        --data-dir "$node_dir" \
        --cluster "$CLUSTER_CONFIG" &
    PIDS+=($!)
    echo "  Started node ${id} (PID ${PIDS[-1]})"
}

# Start all nodes
echo "1. Starting 3-node cluster..."
for i in $(seq 1 $NUM_NODES); do
    start_node "$i"
done
sleep 3 # Wait for leader election

# Write test data
echo "2. Writing test data..."
KEYS_WRITTEN=()
for i in $(seq 1 20); do
    KEY="crash_test_key_${i}"
    VALUE="value_${i}_$(date +%s%N)"
    # Use the loadgen or grpcurl to write
    if "${PROJECT_DIR}/build/raftkv_client" --put "${KEY}=${VALUE}" --cluster "$CLUSTER_CONFIG" 2>/dev/null; then
        KEYS_WRITTEN+=("${KEY}=${VALUE}")
        echo "    Wrote: ${KEY}"
    fi
done
echo "  Written ${#KEYS_WRITTEN[@]} key-value pairs"

# Identify leader (simplistic: try each node)
echo "3. Identifying leader..."
LEADER_ID=""
for i in $(seq 1 $NUM_NODES); do
    STATUS_PORT=$((7110 + i))
    ROLE=$(curl -s "http://127.0.0.1:${STATUS_PORT}/status" 2>/dev/null | grep -o '"role":"[^"]*"' | cut -d'"' -f4 || echo "")
    if [ "$ROLE" = "leader" ]; then
        LEADER_ID=$i
        echo "  Leader is node ${LEADER_ID}"
        break
    fi
done

if [ -z "$LEADER_ID" ]; then
    echo "  WARNING: Could not identify leader, killing node 1"
    LEADER_ID=1
fi

# Kill leader with SIGKILL (no graceful shutdown)
echo "4. Killing leader (node ${LEADER_ID}) with SIGKILL..."
LEADER_PID=${PIDS[$((LEADER_ID - 1))]}
kill -9 "$LEADER_PID" 2>/dev/null || true
sleep 2

# Wait for new leader election
echo "5. Waiting for new leader election..."
sleep 5

# Restart the killed node
echo "6. Restarting node ${LEADER_ID}..."
start_node "$LEADER_ID"
sleep 3

# Verify all acknowledged writes survive
echo "7. Verifying data integrity..."
MISSING=0
for entry in "${KEYS_WRITTEN[@]}"; do
    KEY="${entry%%=*}"
    EXPECTED_VALUE="${entry#*=}"
    # Read from any node
    for i in $(seq 1 $NUM_NODES); do
        ACTUAL=$(${PROJECT_DIR}/build/raftkv_client --get "${KEY}" --cluster "$CLUSTER_CONFIG" 2>/dev/null || echo "")
        if [ "$ACTUAL" = "$EXPECTED_VALUE" ]; then
            break
        fi
    done
    if [ "$ACTUAL" != "$EXPECTED_VALUE" ]; then
        echo "  MISSING: ${KEY} (expected: ${EXPECTED_VALUE})"
        MISSING=$((MISSING + 1))
        PASS=false
    fi
done

echo ""
echo "=== Results ==="
if [ "$PASS" = true ]; then
    echo "PASS: All ${#KEYS_WRITTEN[@]} acknowledged writes survived crash recovery."
    exit 0
else
    echo "FAIL: ${MISSING} writes lost after crash recovery."
    exit 1
fi
