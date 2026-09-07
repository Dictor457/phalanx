# phalanx

A lightweight experimental Layer 4 / Layer 7 reverse proxy and DoS mitigation prototype exploring Linux in-kernel packet filtering via eBPF/XDP and asynchronous user-space I/O via io_uring.

## Overview

PHALANX is an educational systems programming project designed to investigate modern Linux networking primitives and their performance characteristics. 

The goal of this project is to explore how user-space packet processing can be minimized by combining:
1. Early packet drops at the network driver level using eBPF/XDP.
2. An asynchronous, multi-threaded reverse proxy utilizing a shared-nothing model with `io_uring` and `SO_REUSEPORT`.

---

## Architecture

```text
Incoming Packets
       │
       ▼
[ XDP Hook (Driver / Generic) ] ─── (Blacklisted IP) ───> XDP_DROP (~10ns)
       │
   (Clean IP)
       │
       ▼
[ Kernel TCP Stack / SO_REUSEPORT ]
       │
  ┌────┴───────────────────────────┐
  ▼                                ▼
[ Worker Thread 0 ]        [ Worker Thread N ]   (Thread-per-Core, CPU pinned)
  io_uring loop              io_uring loop
  (accept/recv/send)         (accept/recv/send)
  ┌────────────────────────────────┘
  ▼
[ Upstream Pool ] ───> Target Backend (P2C Selection)
```

---

## Key Components

### 1. In-Kernel Packet Filtering (eBPF/XDP)
- Packet headers are parsed in the XDP hook before the kernel allocates an `sk_buff`.
- Source IPv4 addresses are checked against a `BPF_MAP_TYPE_HASH` blacklist.
- Matching packets are discarded immediately via `XDP_DROP`.
- Filter statistics are maintained in a `BPF_MAP_TYPE_ARRAY` for user-space telemetry.

### 2. User-Space Proxy (C++20 & io_uring)
- **Shared-Nothing Concurrency:** Worker threads are pinned to dedicated CPU cores via `pthread_setaffinity_np` to improve cache locality.
- **Independent Rings:** Each thread initializes its own `io_uring` instance and listens on the proxy port via `SO_REUSEPORT`, eliminating cross-thread lock contention on accepts.
- **Load Balancing:** Upstream selection implements the Power of Two Random Choices (P2C) algorithm based on active connection counts.
- **Telemetry:** Injected headers (`X-Phalanx-Worker`, `X-Phalanx-Upstream`, `X-Phalanx-Latency-Us`) provide per-request latency tracking in microseconds.

---

## Project Status & Current Limitations

This project is an experimental prototype intended for learning and benchmarking, not a production-grade reverse proxy. Current architectural limitations include:

- **Simplified HTTP Handling:** The proxy operates on simple HTTP/1.1 request boundaries and does not implement a complete streaming HTTP parser.
- **Transport Security:** No TLS/SSL termination support.
- **Connection Model:** Upstream connections are created per request without connection pooling (keep-alive pipeline).
- **Generic XDP Mode:** Tested primarily under `XDP_FLAGS_SKB_MODE` for cross-platform compatibility across virtual and loopback interfaces.

---

## Building and Running

### Dependencies
- Linux kernel >= 5.15 with eBPF and io_uring support
- `clang` & `llvm` (for BPF bytecode compilation)
- `libbpf`
- `liburing`
- `cmake` (>= 3.20) and `ninja`
- C++20 compliant compiler (GCC >= 11 or Clang >= 13)

### Build
```bash
cmake -B build -G Ninja
cmake --build build
```

### Running

1. Start the mock backend cluster:
```bash
python3 scripts/spawn_backends.py
```
2. Launch the proxy core (root privileges required for attaching XDP programs):
```bash
sudo ./build/phalanx lo
```

### Interactive CLI

The process provides a basic stdin interface for runtime map interaction:

- `block <ip>`: Insert an IPv4 address into the BPF blacklist map.
- `unblock <ip>`: Remove an IPv4 address from the map.
- `stats`: Query packet counters from the kernel telemetry map.
- `exit`: Detach eBPF filter and initiate graceful worker shutdown.

---

## License

GPL-2.0-only
