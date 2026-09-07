#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <cstdint>

namespace phalanx::proxy {

struct Backend {
    std::string host;
    uint16_t port;
    std::atomic<bool> is_alive{true};
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> total_requests{0};

    Backend(std::string h, uint16_t p) : host(std::move(h)), port(p) {}
};

class UpstreamPool {
public:
    UpstreamPool() = default;
    void add_backend(const std::string& host, uint16_t port);
    [[nodiscard]] std::shared_ptr<Backend> get_next_p2c();
    [[nodiscard]] size_t size() const noexcept { return backends_.size(); }

private:
    std::vector<std::shared_ptr<Backend>> backends_;
};

} // namespace phalanx::proxy
