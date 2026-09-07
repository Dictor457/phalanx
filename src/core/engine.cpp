#include "phalanx/core/engine.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <thread>

namespace phalanx::core {

Engine::Engine(std::shared_ptr<proxy::UpstreamPool> upstream_pool, uint32_t thread_count)
    : upstream_pool_(std::move(upstream_pool)) {
    if (thread_count == 0) thread_count = std::thread::hardware_concurrency();
    worker_count_ = thread_count;

    fmt::print("[*] Initializing Shared-Nothing worker pool: {} threads allocated\n", worker_count_);
    workers_.reserve(worker_count_);
    for (uint32_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back(std::make_unique<Worker>(i, upstream_pool_, 8080));
    }
}

Engine::~Engine() { stop(); }

void Engine::start() {
    running_.store(true, std::memory_order_release);
    fmt::print("[>] Starting PHALANX L7 listener on port :8080...\n");
    for (auto& w : workers_) w->start();
}

void Engine::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    fmt::print("\n[*] Stopping proxy engine and releasing network sockets...\n");
    for (auto& w : workers_) w->stop();
    for (auto& w : workers_) w->join();
    fmt::print("[✓] All worker threads terminated cleanly.\n");
}

void Engine::wait() {
    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace phalanx::core
