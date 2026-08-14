#!/usr/bin/env bash
# Launch a local N-node cluster on ports 7101-710N (gRPC) and 7111-711N (HTTP status).
# Usage: ./scripts/cluster_up.sh [num_nodes] [--no-fsync] [--enable-admin]
set -euo pipefail

NUM_NODES=${1:-3}
shift || true

EXTRA_ARGS="$@"
PIDS=()
DATA_DIR="/tmp/raftkv"

cleanup() {
    echo "Stopping cluster..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait
}
trap cleanup EXIT INT TERM

# Build cluster config
CLUSTER_CONFIG=""
for i in $(seq 1 "$NUM_NODES"); do
    PORT=$((7100 + i))
    if [ -n "$CLUSTER_CONFIG" ]; then
        CLUSTER_CONFIG="${CLUSTER_CONFIG},"
    fi
    CLUSTER_CONFIG="${CLUSTER_CONFIG}${i}=127.0.0.1:${PORT}"
done

echo "Starting ${NUM_NODES}-node cluster..."
echo "Cluster config: ${CLUSTER_CONFIG}"

for i in $(seq 1 "$NUM_NODES"); do
    PORT=$((7100 + i))
    STATUS_PORT=$((7110 + i))
    NODE_DATA_DIR="${DATA_DIR}/node${i}"
    mkdir -p "$NODE_DATA_DIR"

    echo "  Node ${i}: gRPC=:${PORT} status=:${STATUS_PORT} data=${NODE_DATA_DIR}"

    ./build/raftkv_server \
        --node-id "$i" \
        --port "$PORT" \
        --status-port "$STATUS_PORT" \
        --data-dir "$NODE_DATA_DIR" \
        --cluster "$CLUSTER_CONFIG" \
        $EXTRA_ARGS &
    PIDS+=($!)
done

echo ""
echo "Cluster running. Press Ctrl+C to stop."
echo "  gRPC ports: 7101-710${NUM_NODES}"
echo "  Status ports: 7111-711${NUM_NODES}"
wait
