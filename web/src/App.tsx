import { useState } from 'react';
import { Topology } from './components/Topology';
import { LogAlignment } from './components/LogAlignment';
import { ChaosPanel } from './components/ChaosPanel';
import { LoadStrip } from './components/LoadStrip';
import { useClusterState } from './hooks/useClusterState';

type Tab = 'topology' | 'log' | 'chaos' | 'load';

export default function App() {
    const [activeTab, setActiveTab] = useState<Tab>('topology');
    const cluster = useClusterState();

    const tabs: { id: Tab; label: string }[] = [
        { id: 'topology', label: 'Topology' },
        { id: 'log', label: 'Log Alignment' },
        { id: 'chaos', label: 'Chaos Panel' },
        { id: 'load', label: 'Load Strip' },
    ];

    return (
        <div className="min-h-screen bg-gray-900">
            <header className="bg-gray-800 border-b border-gray-700 px-6 py-4">
                <div className="flex items-center justify-between">
                    <h1 className="text-xl font-bold text-white">
                        RaftKV Cluster Inspector
                    </h1>
                    <div className="flex items-center gap-4">
                        <span className="text-sm text-gray-400">
                            {cluster.connected
                                ? `${cluster.nodes.length} nodes`
                                : 'Disconnected'}
                        </span>
                        <span
                            className={`w-2 h-2 rounded-full ${
                                cluster.connected ? 'bg-green-500' : 'bg-red-500'
                            }`}
                        />
                    </div>
                </div>

                <nav className="flex gap-1 mt-4">
                    {tabs.map((tab) => (
                        <button
                            key={tab.id}
                            onClick={() => setActiveTab(tab.id)}
                            className={`px-4 py-2 rounded-t text-sm font-medium transition-colors ${
                                activeTab === tab.id
                                    ? 'bg-gray-900 text-white'
                                    : 'bg-gray-700 text-gray-400 hover:text-white'
                            }`}
                        >
                            {tab.label}
                        </button>
                    ))}
                </nav>
            </header>

            <main className="p-6">
                {activeTab === 'topology' && <Topology nodes={cluster.nodes} />}
                {activeTab === 'log' && <LogAlignment nodes={cluster.nodes} />}
                {activeTab === 'chaos' && (
                    <ChaosPanel nodes={cluster.nodes} events={cluster.events} />
                )}
                {activeTab === 'load' && <LoadStrip />}
            </main>
        </div>
    );
}
