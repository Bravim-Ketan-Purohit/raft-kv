export type Role = 'leader' | 'follower' | 'candidate' | 'dead';

export interface PeerInfo {
    nextIndex: number;
    matchIndex: number;
    lastAckMs: number;
}

export interface AllocInfo {
    bytesMapped: number;
    bytesLive: number;
}

export interface LogEntry {
    index: number;
    term: number;
    op: string;
    key?: string;
}

export interface NodeStatus {
    id: number;
    role: Role;
    term: number;
    votedFor: number | null;
    commitIndex: number;
    lastApplied: number;
    logLen: number;
    leaderId: number | null;
    uptimeMs: number;
    logTail: LogEntry[];
    peers: Record<string, PeerInfo>;
    alloc: AllocInfo;
    readMode: string;
}

export interface ClusterEvent {
    timestamp: number;
    nodeId: number;
    type: 'role_change' | 'term_change' | 'commit_advance' | 'peer_timeout';
    detail: string;
}
