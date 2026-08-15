import { useEffect, useState, useRef, useCallback } from 'react';
import { NodeStatus, ClusterEvent } from '../types';

const STATUS_PORTS = [7111, 7112, 7113, 7114, 7115];
const POLL_INTERVAL = 250;

interface ClusterState {
    nodes: NodeStatus[];
    events: ClusterEvent[];
    connected: boolean;
}

export function useClusterState(): ClusterState {
    const [nodes, setNodes] = useState<NodeStatus[]>([]);
    const [events, setEvents] = useState<ClusterEvent[]>([]);
    const [connected, setConnected] = useState(false);
    const eventSourcesRef = useRef<EventSource[]>([]);

    const fetchStatus = useCallback(async () => {
        const results: NodeStatus[] = [];
        let anyConnected = false;

        for (const port of STATUS_PORTS) {
            try {
                const resp = await fetch(`http://127.0.0.1:${port}/status`, {
                    signal: AbortSignal.timeout(500),
                });
                if (resp.ok) {
                    const data = await resp.json();
                    results.push(data);
                    anyConnected = true;
                }
            } catch {
                // Node not reachable
            }
        }

        setNodes(results);
        setConnected(anyConnected);
    }, []);

    // Poll for status
    useEffect(() => {
        fetchStatus();
        const interval = setInterval(fetchStatus, POLL_INTERVAL);
        return () => clearInterval(interval);
    }, [fetchStatus]);

    // SSE for events
    useEffect(() => {
        const sources: EventSource[] = [];

        for (const port of STATUS_PORTS) {
            try {
                const es = new EventSource(`http://127.0.0.1:${port}/events`);
                es.onmessage = (event) => {
                    try {
                        const data: ClusterEvent = JSON.parse(event.data);
                        setEvents((prev) => [...prev.slice(-100), data]);
                    } catch {
                        // Invalid event data
                    }
                };
                es.onerror = () => {
                    // Silent reconnect handled by EventSource
                };
                sources.push(es);
            } catch {
                // Port not available
            }
        }

        eventSourcesRef.current = sources;
        return () => {
            sources.forEach((s) => s.close());
        };
    }, []);

    return { nodes, events, connected };
}
