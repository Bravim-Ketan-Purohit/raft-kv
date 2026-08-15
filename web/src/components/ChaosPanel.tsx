import { useState } from 'react';
import { NodeStatus, ClusterEvent } from '../types';

interface Props {
    nodes: NodeStatus[];
    events: ClusterEvent[];
}

export function ChaosPanel({ nodes, events }: Props) {
    const [adminEnabled, setAdminEnabled] = useState(false);

    const sendAdmin = async (nodeId: number, action: string, body?: object) => {
        const port = 7110 + nodeId;
        try {
            await fetch(`http://127.0.0.1:${port}/admin/${action}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: body ? JSON.stringify(body) : undefined,
            });
        } catch (e) {
            console.error(`Failed to send ${action} to node ${nodeId}:`, e);
        }
    };

    return (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            {/* Controls */}
            <div>
                <h2 className="text-lg font-bold text-white mb-4">Chaos Controls</h2>

                {!adminEnabled && (
                    <div className="bg-yellow-900/30 border border-yellow-700 rounded p-3 mb-4 text-sm text-yellow-300">
                        Admin endpoints are disabled by default. Start nodes with{' '}
                        <code>--enable-admin</code> to use chaos controls.
                        <button
                            onClick={() => setAdminEnabled(true)}
                            className="ml-2 underline hover:text-yellow-100"
                        >
                            I know, show controls anyway
                        </button>
                    </div>
                )}

                <div className="space-y-3">
                    {nodes.map((node) => (
                        <div
                            key={node.id}
                            className="flex items-center gap-2 bg-gray-800 rounded p-3"
                        >
                            <span className="text-white font-mono w-20">
                                Node {node.id}
                            </span>
                            <span className="text-xs text-gray-500 w-16">
                                {node.role}
                            </span>
                            <div className="flex gap-1 ml-auto">
                                <ChaosButton
                                    label="Pause"
                                    onClick={() => sendAdmin(node.id, 'pause')}
                                    color="yellow"
                                />
                                <ChaosButton
                                    label="Resume"
                                    onClick={() => sendAdmin(node.id, 'resume')}
                                    color="green"
                                />
                                <ChaosButton
                                    label="Step Down"
                                    onClick={() => sendAdmin(node.id, 'stepdown')}
                                    color="blue"
                                />
                                <ChaosButton
                                    label="Crash"
                                    onClick={() => sendAdmin(node.id, 'crash')}
                                    color="red"
                                />
                            </div>
                        </div>
                    ))}
                </div>

                {/* Partition controls */}
                <div className="mt-4">
                    <h3 className="text-sm font-bold text-gray-400 mb-2">
                        Network Partitions
                    </h3>
                    {nodes.map((node) => (
                        <div key={node.id} className="flex items-center gap-2 mb-1">
                            <span className="text-xs text-gray-500 w-16">
                                Isolate {node.id}
                            </span>
                            <button
                                onClick={() => {
                                    const others = nodes
                                        .filter((n) => n.id !== node.id)
                                        .map((n) => n.id);
                                    sendAdmin(node.id, 'partition', { from: others });
                                }}
                                className="px-2 py-1 text-xs rounded bg-orange-800 hover:bg-orange-700 text-orange-200"
                            >
                                Partition
                            </button>
                        </div>
                    ))}
                </div>
            </div>

            {/* Event Timeline */}
            <div>
                <h2 className="text-lg font-bold text-white mb-4">Event Timeline</h2>
                <div className="bg-gray-800 rounded p-3 h-96 overflow-y-auto font-mono text-xs">
                    {events.length === 0 ? (
                        <p className="text-gray-600">No events yet.</p>
                    ) : (
                        events
                            .slice()
                            .reverse()
                            .map((event, i) => (
                                <div key={i} className="flex gap-2 mb-1">
                                    <span className="text-gray-600 w-20 shrink-0">
                                        {new Date(event.timestamp).toLocaleTimeString()}
                                    </span>
                                    <span className="text-gray-400 w-12">
                                        N{event.nodeId}
                                    </span>
                                    <span
                                        className={`${
                                            event.type === 'role_change'
                                                ? 'text-yellow-400'
                                                : event.type === 'term_change'
                                                  ? 'text-blue-400'
                                                  : 'text-gray-300'
                                        }`}
                                    >
                                        {event.detail}
                                    </span>
                                </div>
                            ))
                    )}
                </div>
            </div>
        </div>
    );
}

function ChaosButton({
    label,
    onClick,
    color,
}: {
    label: string;
    onClick: () => void;
    color: string;
}) {
    const colorClasses: Record<string, string> = {
        red: 'bg-red-800 hover:bg-red-700 text-red-200',
        yellow: 'bg-yellow-800 hover:bg-yellow-700 text-yellow-200',
        green: 'bg-green-800 hover:bg-green-700 text-green-200',
        blue: 'bg-blue-800 hover:bg-blue-700 text-blue-200',
    };

    return (
        <button
            onClick={onClick}
            className={`px-2 py-1 text-xs rounded ${colorClasses[color]}`}
        >
            {label}
        </button>
    );
}
