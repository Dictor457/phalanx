#include "phalanx/ebpf/xdp_manager.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace phalanx::ebpf {

XdpManager::XdpManager() = default;
XdpManager::~XdpManager() { detach(); }

bool XdpManager::load_and_attach(const std::string& iface_name, const std::string& bpf_obj_path) {
    iface_name_ = iface_name;
    ifindex_ = static_cast<int>(if_nametoindex(iface_name.c_str()));
    if (ifindex_ == 0) {
        fmt::print(stderr, "[-] XDP: Interface '{}' not found\n", iface_name);
        return false;
    }

    bpf_obj_ = bpf_object__open_file(bpf_obj_path.c_str(), nullptr);
    if (!bpf_obj_) {
        fmt::print(stderr, "[-] XDP: Failed to open BPF object file: {}\n", bpf_obj_path);
        return false;
    }

    if (bpf_object__load(bpf_obj_) < 0) {
        fmt::print(stderr, "[-] XDP: Failed to load BPF program into kernel\n");
        bpf_object__close(bpf_obj_);
        bpf_obj_ = nullptr;
        return false;
    }

    struct bpf_program* prog = bpf_object__find_program_by_name(bpf_obj_, "phalanx_xdp_filter");
    if (!prog) {
        fmt::print(stderr, "[-] XDP: Program 'phalanx_xdp_filter' not found in object\n");
        return false;
    }

    prog_fd_ = bpf_program__fd(prog);
    blacklist_map_fd_ = bpf_object__find_map_fd_by_name(bpf_obj_, "blacklist_map");
    stats_map_fd_ = bpf_object__find_map_fd_by_name(bpf_obj_, "stats_map");

    __u32 xdp_flags = XDP_FLAGS_SKB_MODE;
    int ret = bpf_xdp_attach(ifindex_, prog_fd_, xdp_flags, nullptr);
    if (ret < 0) {
        fmt::print(stderr, "[-] XDP: Failed to attach program to {} (code: {})\n", iface_name, ret);
        return false;
    }

    fmt::print(fg(fmt::color::lime_green), "[✓] L4 eBPF/XDP filter attached to '{}' (ifindex: {})\n", iface_name, ifindex_);
    return true;
}

void XdpManager::detach() {
    if (ifindex_ > 0 && prog_fd_ > 0) {
        __u32 xdp_flags = XDP_FLAGS_SKB_MODE;
        bpf_xdp_detach(ifindex_, xdp_flags, nullptr);
        fmt::print("[*] XDP filter detached from interface '{}'\n", iface_name_);
        ifindex_ = -1;
    }
    if (bpf_obj_) {
        bpf_object__close(bpf_obj_);
        bpf_obj_ = nullptr;
    }
}

bool XdpManager::block_ip(const std::string& ip_str) {
    if (blacklist_map_fd_ < 0) return false;
    uint32_t ip_addr = 0;
    if (inet_pton(AF_INET, ip_str.c_str(), &ip_addr) <= 0) return false;

    uint64_t val = 0;
    return bpf_map_update_elem(blacklist_map_fd_, &ip_addr, &val, BPF_ANY) == 0;
}

bool XdpManager::unblock_ip(const std::string& ip_str) {
    if (blacklist_map_fd_ < 0) return false;
    uint32_t ip_addr = 0;
    if (inet_pton(AF_INET, ip_str.c_str(), &ip_addr) <= 0) return false;

    return bpf_map_delete_elem(blacklist_map_fd_, &ip_addr) == 0;
}

XdpStats XdpManager::get_stats() const {
    XdpStats stats{};
    if (stats_map_fd_ < 0) return stats;
    uint32_t key_total = 0, key_drops = 1;
    bpf_map_lookup_elem(stats_map_fd_, &key_total, &stats.total_packets);
    bpf_map_lookup_elem(stats_map_fd_, &key_drops, &stats.dropped_packets);
    return stats;
}

} // namespace phalanx::ebpf
