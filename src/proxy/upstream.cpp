#include "phalanx/proxy/upstream.hpp"
#include <random>

namespace phalanx::proxy {

void UpstreamPool::add_backend(const std::string& host, uint16_t port) {
    backends_.emplace_back(std::make_shared<Backend>(host, port));
}

std::shared_ptr<Backend> UpstreamPool::get_next_p2c() {
    if (backends_.empty()) return nullptr;
    if (backends_.size() == 1) return backends_[0];

    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, backends_.size() - 1);

    size_t idx1 = dist(rng);
    size_t idx2 = dist(rng);
    while (idx2 == idx1) idx2 = dist(rng);

    auto b1 = backends_[idx1];
    auto b2 = backends_[idx2];

    if (!b1->is_alive.load(std::memory_order_relaxed)) return b2;
    if (!b2->is_alive.load(std::memory_order_relaxed)) return b1;

    if (b1->active_connections.load(std::memory_order_relaxed) <= 
        b2->active_connections.load(std::memory_order_relaxed)) {
        return b1;
    }
    return b2;
}

} // namespace phalanx::proxy
