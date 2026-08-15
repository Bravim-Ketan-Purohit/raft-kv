import { NodeStatus } from '../types';

interface Props {
    nodes: NodeStatus[];
}

export function LogAlignment({ nodes }: Props) {
    if (nodes.length === 0) {
        return (
            <div className="flex items-center justify-center h-64 text-gray-500">
                No nodes connected.
            </div>
        );
    }

    // Find the range of indices to display
    const allIndices = new Set<number>();
    for (const node of nodes) {
        for (const entry of node.logTail || []) {
            allIndices.add(entry.index);
        }
    }
    const sortedIndices = [...allIndices].sort((a, b) => a - b);

    if (sortedIndices.length === 0) {
        return (
            <div className="flex items-center justify-center h-64 text-gray-500">
                No log entries to display.
            </div>
        );
    }

    return (
        <div className="overflow-x-auto">
            <table className="w-full border-collapse text-sm">
                <thead>
                    <tr className="border-b border-gray-700">
                        <th className="text-left p-2 text-gray-400">Index</th>
                        {nodes.map((node) => (
                            <th key={node.id} className="text-left p-2 text-gray-400">
                                Node {node.id}
                                <span className="ml-1 text-xs text-gray-600">
                                    ({node.role})
                                </span>
                            </th>
                        ))}
                    </tr>
                </thead>
                <tbody>
                    {sortedIndices.map((index) => {
                        const entries = nodes.map((node) =>
                            (node.logTail || []).find((e) => e.index === index)
                        );

                        // Check if all entries agree on this index
                        const terms = entries
                            .filter((e) => e !== undefined)
                            .map((e) => e!.term);
                        const divergent = new Set(terms).size > 1;

                        return (
                            <tr
                                key={index}
                                className={`border-b border-gray-800 ${
                                    divergent ? 'bg-red-900/20' : ''
                                }`}
                            >
                                <td className="p-2 font-mono text-gray-400">
                                    {index}
                                </td>
                                {entries.map((entry, i) => (
                                    <td
                                        key={i}
                                        className={`p-2 font-mono ${
                                            divergent && entry
                                                ? 'text-red-400'
                                                : 'text-gray-300'
                                        }`}
                                    >
                                        {entry ? (
                                            <span>
                                                <span className="text-xs text-gray-500">
                                                    T{entry.term}
                                                </span>{' '}
                                                {entry.op}
                                                {entry.key && (
                                                    <span className="text-gray-500">
                                                        ({entry.key})
                                                    </span>
                                                )}
                                            </span>
                                        ) : (
                                            <span className="text-gray-700">-</span>
                                        )}
                                    </td>
                                ))}
                            </tr>
                        );
                    })}
                </tbody>
            </table>
        </div>
    );
}
