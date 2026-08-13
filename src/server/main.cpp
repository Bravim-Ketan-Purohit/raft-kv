// RaftKV Node — main entry point
// Usage: raftkv_server --node-id N --port P --status-port P --data-dir DIR --cluster SPEC
//
// Cluster spec: "1=host:port,2=host:port,3=host:port"

#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "raftkv/server.h"

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int) {
    g_shutdown.store(true);
}

static std::vector<raftkv::NodeConfig> parse_cluster(const std::string& spec) {
    std::vector<raftkv::NodeConfig> configs;
    std::string remaining = spec;

    while (!remaining.empty()) {
        size_t comma = remaining.find(',');
        std::string item = (comma != std::string::npos) ? remaining.substr(0, comma)
                                                         : remaining;
        remaining = (comma != std::string::npos) ? remaining.substr(comma + 1) : "";

        size_t eq = item.find('=');
        if (eq == std::string::npos) continue;

        raftkv::NodeConfig nc;
        nc.id = static_cast<raftkv::NodeId>(std::stoul(item.substr(0, eq)));
        nc.address = item.substr(eq + 1);
        // Derive status address from port offset (+10)
        size_t colon = nc.address.rfind(':');
        if (colon != std::string::npos) {
            int port = std::stoi(nc.address.substr(colon + 1));
            nc.status_address = nc.address.substr(0, colon + 1) + std::to_string(port + 10);
        }
        configs.push_back(nc);
    }

    return configs;
}

int main(int argc, char* argv[]) {
    raftkv::ServerConfig config;
    config.node_id = 1;
    config.data_dir = "/tmp/raftkv/node1";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--node-id" && i + 1 < argc)
            config.node_id = static_cast<raftkv::NodeId>(std::stoul(argv[++i]));
        else if (arg == "--port" && i + 1 < argc)
            ++i; // Port is embedded in cluster config
        else if (arg == "--status-port" && i + 1 < argc)
            ++i; // Status port derived from cluster config
        else if (arg == "--data-dir" && i + 1 < argc)
            config.data_dir = argv[++i];
        else if (arg == "--cluster" && i + 1 < argc)
            config.cluster = parse_cluster(argv[++i]);
        else if (arg == "--no-fsync")
            config.no_fsync = true;
        else if (arg == "--enable-admin")
            config.enable_admin = true;
        else if (arg == "--election-timeout" && i + 1 < argc)
            config.election_timeout_ms = std::stoul(argv[++i]);
        else if (arg == "--heartbeat-interval" && i + 1 < argc)
            config.heartbeat_interval_ms = std::stoul(argv[++i]);
        else if (arg == "--otel-exporter" && i + 1 < argc)
            config.otel_exporter = argv[++i];
    }

    if (config.cluster.empty()) {
        std::cerr << "Error: --cluster is required\n";
        std::cerr << "Usage: raftkv_server --node-id N --cluster '1=host:port,2=host:port,...'\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[node " << config.node_id << "] Starting RaftKV server\n";
    std::cout << "  Data dir: " << config.data_dir << "\n";
    std::cout << "  Cluster: " << config.cluster.size() << " nodes\n";
    std::cout << "  Fsync: " << (config.no_fsync ? "OFF" : "ON") << "\n";
    std::cout << "  Admin: " << (config.enable_admin ? "ON" : "OFF") << "\n";
    std::cout << "  OTel: " << config.otel_exporter << "\n";

    raftkv::Server server(config);
    server.run();

    return 0;
}
