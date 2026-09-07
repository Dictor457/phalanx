#pragma once
#include "phalanx/core/worker.hpp"
#include "phalanx/proxy/upstream.hpp"
#include <vector>
#include <memory>
#include <atomic>

namespace phalanx::core {

class Engine {
public:
    explicit Engine(std::shared_ptr<proxy::UpstreamPool> upstream_pool, uint32_t thread_count = 0);
    ~Engine();

    void start();
    void stop();
    void wait();

private:
    std::shared_ptr<proxy::UpstreamPool> upstream_pool_;
    uint32_t worker_count_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> running_{false};
};

} // namespace phalanx::core
