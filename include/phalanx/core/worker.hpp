#pragma once
#include "phalanx/proxy/upstream.hpp"
#include <atomic>
#include <thread>
#include <liburing.h>
#include <cstdint>
#include <memory>

namespace phalanx::core {

enum class EventType : uint8_t { ACCEPT = 0, CLIENT_READ = 1, CLIENT_WRITE = 2 };

struct ConnContext {
    int client_fd{-1};
    EventType event_type{EventType::ACCEPT};
    char buffer[4096]{};
    size_t bytes_transferred{0};
};

class Worker {
public:
    explicit Worker(uint32_t id, std::shared_ptr<proxy::UpstreamPool> upstream_pool, uint16_t port = 8080, uint32_t queue_depth = 1024);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;

    void start();
    void stop();
    void join();
    [[nodiscard]] uint32_t get_id() const noexcept { return id_; }

private:
    void run();
    void pin_to_core(uint32_t core_id);
    void init_listener();
    void submit_accept();
    void handle_proxy_request(ConnContext* ctx, int bytes_read);

    uint32_t id_;
    std::shared_ptr<proxy::UpstreamPool> upstream_pool_;
    uint16_t port_;
    uint32_t queue_depth_;
    int listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread thread_;
    struct io_uring ring_{};
    ConnContext accept_ctx_{};
};

} // namespace phalanx::core
