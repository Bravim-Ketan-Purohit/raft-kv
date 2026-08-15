import { useEffect, useState } from 'react';

interface BenchResult {
    timestamp: string;
    config: {
        nodes: number;
        threads: number;
        read_ratio: number;
        read_mode: string;
        fsync: string;
        otel_exporter: string;
    };
    results: {
        elapsed_secs: number;
        read_ops: number;
        write_ops: number;
        read_throughput_ops_sec: number;
        write_throughput_ops_sec: number;
        read_latency_us: {
            p50: number;
            p95: number;
            p99: number;
            p999: number;
            mean: number;
        };
        write_latency_us: {
            p50: number;
            p95: number;
            p99: number;
            p999: number;
            mean: number;
        };
    };
}

export function LoadStrip() {
    const [results, setResults] = useState<BenchResult[]>([]);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        // In production, this would load from bench/results/*.json
        // For now, show a placeholder or try to fetch
        setLoading(false);
    }, []);

    if (loading) {
        return (
            <div className="flex items-center justify-center h-64 text-gray-500">
                Loading benchmark results...
            </div>
        );
    }

    if (results.length === 0) {
        return (
            <div className="bg-gray-800 rounded-lg p-6">
                <h2 className="text-lg font-bold text-white mb-4">
                    Benchmark Results
                </h2>
                <p className="text-gray-400 mb-4">
                    No benchmark results found. Run the load generator:
                </p>
                <code className="text-sm text-green-400 bg-gray-900 p-3 rounded block">
                    ./build/bench/loadgen --nodes 3 --threads 8 --read-ratio 0.95
                    --duration 60s
                </code>
                <p className="text-gray-500 text-sm mt-3">
                    Results will be saved to <code>bench/results/</code> and displayed
                    here.
                </p>
            </div>
        );
    }

    const latest = results[results.length - 1];

    return (
        <div className="space-y-6">
            <div className="bg-gray-800 rounded-lg p-6">
                <h2 className="text-lg font-bold text-white mb-4">
                    Latest Benchmark: {latest.timestamp}
                </h2>

                <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                    <MetricCard
                        label="Read Throughput"
                        value={`${(latest.results.read_throughput_ops_sec / 1000).toFixed(1)}K`}
                        unit="ops/sec"
                    />
                    <MetricCard
                        label="Write Throughput"
                        value={`${(latest.results.write_throughput_ops_sec / 1000).toFixed(1)}K`}
                        unit="ops/sec"
                    />
                    <MetricCard
                        label="Read p99"
                        value={`${(latest.results.read_latency_us.p99 / 1000).toFixed(2)}`}
                        unit="ms"
                    />
                    <MetricCard
                        label="Write p99"
                        value={`${(latest.results.write_latency_us.p99 / 1000).toFixed(2)}`}
                        unit="ms"
                    />
                </div>

                <div className="mt-4 text-xs text-gray-500">
                    Config: {latest.config.nodes} nodes, {latest.config.threads}{' '}
                    threads, {latest.config.read_ratio * 100}% reads,{' '}
                    {latest.config.read_mode}, fsync={latest.config.fsync},
                    otel={latest.config.otel_exporter}
                </div>
            </div>
        </div>
    );
}

function MetricCard({
    label,
    value,
    unit,
}: {
    label: string;
    value: string;
    unit: string;
}) {
    return (
        <div className="bg-gray-900 rounded p-3">
            <p className="text-xs text-gray-500">{label}</p>
            <p className="text-2xl font-bold text-white">
                {value}
                <span className="text-sm text-gray-500 ml-1">{unit}</span>
            </p>
        </div>
    );
}
