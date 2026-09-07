#pragma once
#include <string>
#include <cstdint>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

namespace phalanx::ebpf {

struct XdpStats {
    uint64_t total_packets{0};
    uint64_t dropped_packets{0};
};

class XdpManager {
public:
    XdpManager();
    ~XdpManager();

    bool load_and_attach(const std::string& iface_name, const std::string& bpf_obj_path);
    void detach();
    bool block_ip(const std::string& ip_str);
    bool unblock_ip(const std::string& ip_str);
    [[nodiscard]] XdpStats get_stats() const;

private:
    std::string iface_name_;
    int ifindex_{-1};
    struct bpf_object* bpf_obj_{nullptr};
    int prog_fd_{-1};
    int blacklist_map_fd_{-1};
    int stats_map_fd_{-1};
};

} // namespace phalanx::ebpf
