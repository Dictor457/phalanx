#include "phalanx/core/worker.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>

namespace phalanx::core {

Worker::Worker(uint32_t id, std::shared_ptr<proxy::UpstreamPool> upstream_pool, uint16_t port, uint32_t queue_depth)
    : id_(id), upstream_pool_(std::move(upstream_pool)), port_(port), queue_depth_(queue_depth) {
    int ret = io_uring_queue_init(queue_depth_, &ring_, 0);
    if (ret < 0) {
        throw std::runtime_error(fmt::format("Worker #{}: io_uring_queue_init failed: {}", id_, strerror(-ret)));
    }
    init_listener();
}

Worker::~Worker() {
    stop();
    join();
    if (listen_fd_ >= 0) close(listen_fd_);
    io_uring_queue_exit(&ring_);
}

void Worker::pin_to_core(uint32_t core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
        fmt::print(fg(fmt::color::green), "[+] Worker #{}: locked to CPU core {} (SO_REUSEPORT active on port {})\n", id_, core_id, port_);
    }
}

void Worker::init_listener() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(fmt::format("Worker #{}: socket() failed: {}", id_, strerror(errno)));
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(fmt::format("Worker #{}: bind() failed: {}", id_, strerror(errno)));
    }
    if (listen(listen_fd_, 4096) < 0) {
        throw std::runtime_error(fmt::format("Worker #{}: listen() failed: {}", id_, strerror(errno)));
    }
}

void Worker::submit_accept() {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;

    accept_ctx_.client_fd = listen_fd_;
    accept_ctx_.event_type = EventType::ACCEPT;

    io_uring_prep_accept(sqe, listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    io_uring_sqe_set_data(sqe, &accept_ctx_);
    io_uring_submit(&ring_);
}

void Worker::start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&Worker::run, this);
}

void Worker::stop() {
    running_.store(false, std::memory_order_release);
}

void Worker::join() {
    if (thread_.joinable()) thread_.join();
}

void Worker::handle_proxy_request(ConnContext* ctx, int bytes_read) {
    auto start_time = std::chrono::steady_clock::now();
    auto target = upstream_pool_->get_next_p2c();
    if (!target) {
        std::string err = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 21\r\nConnection: close\r\n\r\nNo backends available";
        std::memcpy(ctx->buffer, err.data(), err.size());
        ctx->bytes_transferred = err.size();
        return;
    }

    target->active_connections.fetch_add(1, std::memory_order_relaxed);
    target->total_requests.fetch_add(1, std::memory_order_relaxed);

    int upstream_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in up_addr{};
    up_addr.sin_family = AF_INET;
    up_addr.sin_port = htons(target->port);
    inet_pton(AF_INET, target->host.c_str(), &up_addr.sin_addr);

    struct timeval tv{1, 0};
    setsockopt(upstream_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(upstream_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool success = false;
    std::string backend_payload;

    if (connect(upstream_fd, reinterpret_cast<sockaddr*>(&up_addr), sizeof(up_addr)) == 0) {
        send(upstream_fd, ctx->buffer, static_cast<size_t>(bytes_read), 0);
        char up_buf[4096];
        ssize_t chunk = 0;
        while ((chunk = recv(upstream_fd, up_buf, sizeof(up_buf), 0)) > 0) {
            backend_payload.append(up_buf, static_cast<size_t>(chunk));
        }
        if (!backend_payload.empty()) {
            success = true;
        }
    }
    close(upstream_fd);
    target->active_connections.fetch_sub(1, std::memory_order_relaxed);

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();

    if (success) {
        std::string extra_headers = fmt::format(
            "X-Phalanx-Worker: #{}\r\n"
            "X-Phalanx-Upstream: {}:{}\r\n"
            "X-Phalanx-Latency-Us: {}\r\n",
            id_, target->host, target->port, elapsed_us
        );
        size_t first_crlf = backend_payload.find("\r\n");
        if (first_crlf != std::string::npos) {
            backend_payload.insert(first_crlf + 2, extra_headers);
        }
        ctx->bytes_transferred = backend_payload.size();
        std::memcpy(ctx->buffer, backend_payload.data(), backend_payload.size());
    } else {
        std::string bad_gateway = fmt::format(
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Server: phalanx/0.1.0\r\n"
            "X-Phalanx-Worker: #{}\r\n"
            "Content-Length: 26\r\n"
            "Connection: close\r\n\r\nBackend connection failed\n",
            id_
        );
        ctx->bytes_transferred = bad_gateway.size();
        std::memcpy(ctx->buffer, bad_gateway.data(), bad_gateway.size());
    }
}

void Worker::run() {
    pin_to_core(id_);
    submit_accept();

    struct __kernel_timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = 100'000'000;

    while (running_.load(std::memory_order_relaxed)) {
        struct io_uring_cqe* cqe = nullptr;
        int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
        if (ret < 0 || !cqe) continue;

        auto* ctx = static_cast<ConnContext*>(io_uring_cqe_get_data(cqe));
        int res = cqe->res;
        io_uring_cqe_seen(&ring_, cqe);

        if (!ctx || res < 0) {
            if (ctx && ctx->event_type == EventType::ACCEPT) submit_accept();
            continue;
        }

        switch (ctx->event_type) {
            case EventType::ACCEPT: {
                int client_fd = res;
                submit_accept();

                auto* client_ctx = new ConnContext();
                client_ctx->client_fd = client_fd;
                client_ctx->event_type = EventType::CLIENT_READ;

                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
                if (sqe) {
                    io_uring_prep_recv(sqe, client_fd, client_ctx->buffer, sizeof(client_ctx->buffer) - 1, 0);
                    io_uring_sqe_set_data(sqe, client_ctx);
                    io_uring_submit(&ring_);
                } else {
                    close(client_fd);
                    delete client_ctx;
                }
                break;
            }
            case EventType::CLIENT_READ: {
                int bytes_read = res;
                if (bytes_read <= 0) {
                    close(ctx->client_fd);
                    delete ctx;
                    break;
                }

                handle_proxy_request(ctx, bytes_read);

                ctx->event_type = EventType::CLIENT_WRITE;
                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
                if (sqe) {
                    io_uring_prep_send(sqe, ctx->client_fd, ctx->buffer, ctx->bytes_transferred, 0);
                    io_uring_sqe_set_data(sqe, ctx);
                    io_uring_submit(&ring_);
                } else {
                    close(ctx->client_fd);
                    delete ctx;
                }
                break;
            }
            case EventType::CLIENT_WRITE: {
                close(ctx->client_fd);
                delete ctx;
                break;
            }
        }
    }
}

} // namespace phalanx::core
