import { NodeStatus, Role } from '../types';

interface Props {
    nodes: NodeStatus[];
}

const roleColors: Record<Role, string> = {
    leader: 'border-leader bg-leader/10',
    follower: 'border-follower bg-follower/10',
    candidate: 'border-candidate bg-candidate/10',
    dead: 'border-dead bg-dead/10',
};

const roleBadgeColors: Record<Role, string> = {
    leader: 'bg-leader text-white',
    follower: 'bg-follower text-white',
    candidate: 'bg-candidate text-black',
    dead: 'bg-dead text-white',
};

export function Topology({ nodes }: Props) {
    if (nodes.length === 0) {
        return (
            <div className="flex items-center justify-center h-64 text-gray-500">
                <p>No nodes connected. Start the cluster with <code className="text-gray-300">./scripts/cluster_up.sh</code></p>
            </div>
        );
    }

    return (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
            {nodes.map((node) => (
                <NodeCard key={node.id} node={node} />
            ))}
        </div>
    );
}

function NodeCard({ node }: { node: NodeStatus }) {
    const role = node.role as Role;

    return (
        <div className={`border-2 rounded-lg p-4 ${roleColors[role]}`}>
            <div className="flex items-center justify-between mb-3">
                <h3 className="text-lg font-bold text-white">Node {node.id}</h3>
                <span className={`px-2 py-1 rounded text-xs font-bold ${roleBadgeColors[role]}`}>
                    {role.toUpperCase()}
                </span>
            </div>

            <div className="grid grid-cols-2 gap-2 text-sm">
                <Stat label="Term" value={node.term} />
                <Stat label="Commit" value={node.commitIndex} />
                <Stat label="Applied" value={node.lastApplied} />
                <Stat label="Log Len" value={node.logLen} />
                <Stat label="Voted For" value={node.votedFor ?? '-'} />
                <Stat label="Leader" value={node.leaderId ?? '-'} />
            </div>

            {node.peers && Object.keys(node.peers).length > 0 && (
                <div className="mt-3 border-t border-gray-700 pt-2">
                    <h4 className="text-xs font-bold text-gray-400 mb-1">PEERS</h4>
                    {Object.entries(node.peers).map(([peerId, info]) => (
                        <div key={peerId} className="flex justify-between text-xs text-gray-300">
                            <span>Node {peerId}</span>
                            <span>
                                match:{info.matchIndex} next:{info.nextIndex} ack:{info.lastAckMs}ms
                            </span>
                        </div>
                    ))}
                </div>
            )}

            {node.alloc && (
                <div className="mt-2 text-xs text-gray-500">
                    Alloc: {(node.alloc.bytesLive / 1024).toFixed(0)}KB live / {(node.alloc.bytesMapped / 1024 / 1024).toFixed(1)}MB mapped
                </div>
            )}
        </div>
    );
}

function Stat({ label, value }: { label: string; value: string | number }) {
    return (
        <div>
            <span className="text-gray-500 text-xs">{label}</span>
            <p className="text-white font-mono">{value}</p>
        </div>
    );
}
