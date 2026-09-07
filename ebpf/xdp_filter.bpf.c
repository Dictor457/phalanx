#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, __u64);
} blacklist_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats_map SEC(".maps");

SEC("xdp")
int phalanx_xdp_filter(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    __u32 total_key = 0;
    __u32 drop_key = 1;

    __u64 *total_pkts = bpf_map_lookup_elem(&stats_map, &total_key);
    if (total_pkts) {
        __sync_fetch_and_add(total_pkts, 1);
    }

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return XDP_PASS;
    if (eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) return XDP_PASS;

    __u32 src_ip = ip->saddr;

    __u64 *drop_count = bpf_map_lookup_elem(&blacklist_map, &src_ip);
    if (drop_count) {
        __sync_fetch_and_add(drop_count, 1);
        __u64 *total_drops = bpf_map_lookup_elem(&stats_map, &drop_key);
        if (total_drops) {
            __sync_fetch_and_add(total_drops, 1);
        }
        return XDP_DROP;
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
