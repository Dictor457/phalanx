#include <iostream>
#include <functional>
#include <csignal>
#include <string>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include "phalanx/core/engine.hpp"
#include "phalanx/proxy/upstream.hpp"
#include "phalanx/ebpf/xdp_manager.hpp"

namespace {
    std::function<void(int)> shutdown_handler;
    void signal_bridge(int signal) {
        if (shutdown_handler) shutdown_handler(signal);
        _exit(0);
    }
}

int main(int argc, char* argv[]) {
    fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, 
        "\n  ██████╗ ██╗  ██╗ █████╗ ██╗      █████╗ ███╗   ██╗██╗  ██╗\n"
        "  ██╔══██╗██║  ██║██╔══██╗██║     ██╔══██╗████╗  ██║╚██╗██╔╝\n"
        "  ██████╔╝███████║███████║██║     ███████║██╔██╗ ██║ ╚███╔╝ \n"
        "  ██╔═══╝ ██╔══██║██╔══██║██║     ██╔══██║██║╚██╗██║ ██╔██╗ \n"
        "  ██║     ██║  ██║██║  ██║███████╗██║  ██║██║ ╚████║██╔╝ ██╗\n"
        "  ╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝\n"
    );
    fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold,
        "  -- High-Performance L4/L7 Reverse Proxy & Load Balancer --\n\n");

    std::string iface = (argc > 1) ? argv[1] : "lo";

    try {
        auto upstream_pool = std::make_shared<phalanx::proxy::UpstreamPool>();
        upstream_pool->add_backend("127.0.0.1", 9001);
        upstream_pool->add_backend("127.0.0.1", 9002);
        upstream_pool->add_backend("127.0.0.1", 9003);
        fmt::print("[+] Registered {} upstream backends: ports 9001, 9002, 9003\n", upstream_pool->size());

        auto xdp = std::make_shared<phalanx::ebpf::XdpManager>();
        bool xdp_ok = xdp->load_and_attach(iface, "build/xdp_filter.o");
        if (!xdp_ok) {
            fmt::print(fg(fmt::color::yellow), "[!] Warning: Run with sudo for eBPF/XDP kernel filtering\n");
        }

        phalanx::core::Engine engine(upstream_pool);

        shutdown_handler = [&](int) {
            static std::atomic<bool> already_done{false};
            if (already_done.exchange(true)) return;

            if (xdp_ok) {
                auto stats = xdp->get_stats();
                fmt::print(fg(fmt::color::cyan), "\n[+] XDP Stats: Total packets: {}, Dropped (XDP_DROP): {}\n", 
                    stats.total_packets, stats.dropped_packets);
                xdp->detach();
            }
            engine.stop();
        };

        struct sigaction sa{};
        sa.sa_handler = signal_bridge;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        engine.start();

        fmt::print(fg(fmt::color::lime_green), 
            "\n[+] PHALANX stack active on '{}' (L4) and ':8080' (L7)\n"
            "    CLI Commands: block <ip> | unblock <ip> | stats | help | exit\n\n", iface);

        std::string line;
        while (std::cout << "\033[1;31mphalanx-cli# \033[0m" && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string cmd;
            ss >> cmd;

            if (cmd == "exit" || cmd == "quit") {
                break;
            } else if (cmd == "block") {
                std::string ip;
                if (ss >> ip) {
                    if (xdp->block_ip(ip)) {
                        fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, "[L4 SHIELD] IP {} added to kernel blacklist (XDP_DROP)\n", ip);
                    } else {
                        fmt::print(fg(fmt::color::yellow), "[-] Failed to block IP {}\n", ip);
                    }
                }
            } else if (cmd == "unblock") {
                std::string ip;
                if (ss >> ip) {
                    if (xdp->unblock_ip(ip)) {
                        fmt::print(fg(fmt::color::green), "[✓] IP {} removed from blacklist\n", ip);
                    }
                }
            } else if (cmd == "stats") {
                auto stats = xdp->get_stats();
                fmt::print(fg(fmt::color::steel_blue),
                    "+--- [ Kernel XDP Telemetry ] ---+\n"
                    "| Total L4 Packets:      {:<8} |\n"
                    "| Dropped (XDP_DROP):    {:<8} |\n"
                    "| Forwarded to L7 Proxy: {:<8} |\n"
                    "+--------------------------------+\n",
                    stats.total_packets, stats.dropped_packets,
                    (stats.total_packets >= stats.dropped_packets ? stats.total_packets - stats.dropped_packets : 0)
                );
            } else if (cmd == "help") {
                fmt::print("Commands:\n  block <ip>   - Add IP to kernel-space drop map\n  unblock <ip> - Remove IP from drop map\n  stats        - Read packet counters from BPF map\n  exit         - Graceful shutdown\n");
            }
        }

        if (shutdown_handler) shutdown_handler(0);

    } catch (const std::exception& ex) {
        fmt::print(stderr, "[-] Fatal error: {}\n", ex.what());
        return 1;
    }
    return 0;
}
